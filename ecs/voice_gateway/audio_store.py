from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass
import secrets
import time
from typing import Callable, Optional


@dataclass(frozen=True)
class StoredAudio:
    device_id: str
    pcm: bytes
    expires_at: float


@dataclass(frozen=True)
class StoredTurn:
    device_id: str
    response: dict
    expires_at: float


class MemoryAudioStore:
    def __init__(
        self,
        *,
        max_items: int = 8,
        ttl_seconds: int = 60,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self._max_items = max_items
        self._ttl_seconds = ttl_seconds
        self._clock = clock
        self._audio: OrderedDict[str, StoredAudio] = OrderedDict()
        self._turns: OrderedDict[tuple[str, str], StoredTurn] = OrderedDict()

    @property
    def ttl_seconds(self) -> int:
        return self._ttl_seconds

    def put_audio(self, device_id: str, pcm: bytes) -> str:
        self._purge()
        audio_id = secrets.token_hex(16)
        self._audio[audio_id] = StoredAudio(
            device_id=device_id,
            pcm=pcm,
            expires_at=self._clock() + self._ttl_seconds,
        )
        self._trim(self._audio)
        return audio_id

    def get_audio(self, audio_id: str, device_id: str) -> Optional[bytes]:
        self._purge()
        entry = self._audio.get(audio_id)
        if entry is None or entry.device_id != device_id:
            return None
        self._audio.move_to_end(audio_id)
        return entry.pcm

    def put_turn(self, device_id: str, turn_id: str, response: dict) -> None:
        self._purge()
        key = (device_id, turn_id)
        self._turns[key] = StoredTurn(
            device_id=device_id,
            response=dict(response),
            expires_at=self._clock() + self._ttl_seconds,
        )
        self._turns.move_to_end(key)
        self._trim(self._turns)

    def get_turn(self, device_id: str, turn_id: str) -> Optional[dict]:
        self._purge()
        key = (device_id, turn_id)
        entry = self._turns.get(key)
        if entry is None:
            return None
        self._turns.move_to_end(key)
        return dict(entry.response)

    def _purge(self) -> None:
        now = self._clock()
        for key in [key for key, value in self._audio.items() if value.expires_at <= now]:
            del self._audio[key]
        for key in [key for key, value in self._turns.items() if value.expires_at <= now]:
            del self._turns[key]

    def _trim(self, entries: OrderedDict) -> None:
        while len(entries) > self._max_items:
            entries.popitem(last=False)
