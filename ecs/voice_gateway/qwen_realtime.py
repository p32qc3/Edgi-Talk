from __future__ import annotations

import asyncio
import audioop
import base64
import binascii
from dataclasses import dataclass
import json
from typing import Any, Callable, Optional

from .config import Settings


MODEL_ID = "qwen-audio-3.0-realtime-flash"
INPUT_CHUNK_BYTES = 3200
MAX_OUTPUT_24K_BYTES = 24000 * 2 * 12


class QwenError(RuntimeError):
    pass


class QwenProviderError(QwenError):
    pass


class QwenProtocolError(QwenError):
    pass


class QwenTimeoutError(QwenError):
    pass


@dataclass(frozen=True)
class QwenTurn:
    transcript: str
    reply_text: str
    pcm16: bytes


class QwenRealtime:
    def __init__(
        self,
        settings: Settings,
        *,
        connect: Optional[Callable[..., Any]] = None,
        timeout_seconds: float = 20.0,
    ) -> None:
        if connect is None:
            import websockets

            connect = websockets.connect
        self._settings = settings
        self._connect = connect
        self._timeout_seconds = timeout_seconds

    @property
    def url(self) -> str:
        return (
            f"wss://{self._settings.workspace_id}.cn-beijing.maas.aliyuncs.com/"
            f"api-ws/v1/realtime?model={MODEL_ID}"
        )

    async def complete_turn(self, pcm: bytes) -> QwenTurn:
        try:
            return await asyncio.wait_for(self._complete_turn(pcm), self._timeout_seconds)
        except asyncio.TimeoutError as exc:
            raise QwenTimeoutError("Qwen response timed out") from exc

    async def _complete_turn(self, pcm: bytes) -> QwenTurn:
        headers = {"Authorization": f"Bearer {self._settings.api_key}"}
        async with self._connect(self.url, additional_headers=headers) as socket:
            await self._send(
                socket,
                {
                    "type": "session.update",
                    "session": {
                        "modalities": ["audio", "text"],
                        "voice": "longanqian",
                        "turn_detection": None,
                        "instructions": (
                            "你是电子宠物小团子。请用简短、自然、适合中文口语播放的句子回答，"
                            "每次最多两句话，不要声称已经执行硬件动作。"
                        ),
                    },
                },
            )
            for offset in range(0, len(pcm), INPUT_CHUNK_BYTES):
                await self._send(
                    socket,
                    {
                        "type": "input_audio_buffer.append",
                        "audio": base64.b64encode(
                            pcm[offset : offset + INPUT_CHUNK_BYTES]
                        ).decode("ascii"),
                    },
                )
            await self._send(socket, {"type": "input_audio_buffer.commit"})
            await self._send(
                socket,
                {"type": "response.create", "response": {"modalities": ["audio", "text"]}},
            )

            transcript = ""
            reply_text = ""
            output = bytearray()
            async for raw_message in socket:
                event = self._decode_event(raw_message)
                event_type = event.get("type")
                if event_type == "error":
                    error = event.get("error") or {}
                    code = error.get("code") or "unknown"
                    raise QwenProviderError(f"Qwen error: {code}")
                if event_type == "conversation.item.input_audio_transcription.failed":
                    raise QwenProviderError("Qwen input transcription failed")
                if event_type == "conversation.item.input_audio_transcription.completed":
                    transcript = str(event.get("transcript") or "")
                elif event_type == "response.audio_transcript.done":
                    reply_text = str(event.get("transcript") or "")
                elif event_type == "response.audio.delta":
                    chunk = self._decode_audio(event.get("delta"))
                    remaining = MAX_OUTPUT_24K_BYTES - len(output)
                    if remaining > 0:
                        output.extend(chunk[:remaining])
                elif event_type == "response.done":
                    status = (event.get("response") or {}).get("status")
                    if status != "completed":
                        raise QwenProviderError(f"Qwen response {status or 'invalid'}")
                    if not transcript or not reply_text or not output:
                        raise QwenProtocolError("Qwen response is incomplete")
                    pcm16, _state = audioop.ratecv(
                        bytes(output), 2, 1, 24000, 16000, None
                    )
                    return QwenTurn(
                        transcript=transcript,
                        reply_text=reply_text,
                        pcm16=pcm16,
                    )
        raise QwenProtocolError("Qwen connection closed before response.done")

    @staticmethod
    async def _send(socket: Any, event: dict[str, Any]) -> None:
        await socket.send(json.dumps(event, ensure_ascii=False, separators=(",", ":")))

    @staticmethod
    def _decode_event(raw_message: Any) -> dict[str, Any]:
        try:
            event = json.loads(raw_message)
        except (TypeError, json.JSONDecodeError) as exc:
            raise QwenProtocolError("Qwen returned invalid JSON") from exc
        if not isinstance(event, dict):
            raise QwenProtocolError("Qwen returned a non-object event")
        return event

    @staticmethod
    def _decode_audio(value: Any) -> bytes:
        if not isinstance(value, str):
            raise QwenProtocolError("Qwen audio delta is missing")
        try:
            return base64.b64decode(value, validate=True)
        except (binascii.Error, ValueError) as exc:
            raise QwenProtocolError("Qwen audio delta is invalid") from exc
