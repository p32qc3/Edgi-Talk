from __future__ import annotations

import asyncio
import hmac
import re
import time
from typing import Callable, Optional

from fastapi import FastAPI, Header, Request
from fastapi.responses import JSONResponse, Response

from .audio_store import MemoryAudioStore
from .config import Settings
from .intent import classify_intent
from .qwen_realtime import (
    QwenError,
    QwenProviderError,
    QwenRealtime,
    QwenTimeoutError,
)


MIN_AUDIO_BYTES = 640
MAX_AUDIO_BYTES = 256000
TURN_ID_RE = re.compile(r"^[A-Za-z0-9-]{1,64}$")


def _error(code: str, status_code: int) -> JSONResponse:
    return JSONResponse({"error": code}, status_code=status_code)


def create_app(
    settings: Settings,
    *,
    qwen: Optional[QwenRealtime] = None,
    clock: Callable[[], float] = time.monotonic,
) -> FastAPI:
    app = FastAPI(title="Edgi-Talk Voice Gateway", docs_url=None, redoc_url=None)
    provider = qwen or QwenRealtime(settings)
    store = MemoryAudioStore(clock=clock)
    provider_lock = asyncio.Lock()

    def authenticated(device_id: Optional[str], device_token: Optional[str]) -> bool:
        return bool(
            device_id
            and device_token
            and hmac.compare_digest(device_id, settings.device_id)
            and hmac.compare_digest(device_token, settings.device_token)
        )

    @app.get("/healthz")
    async def healthz():
        return {"status": "ok"}

    @app.post("/v1/voice/turn")
    async def voice_turn(
        request: Request,
        x_device_id: Optional[str] = Header(default=None),
        x_device_token: Optional[str] = Header(default=None),
        x_turn_id: Optional[str] = Header(default=None),
        x_sample_rate: Optional[str] = Header(default=None),
    ):
        if not authenticated(x_device_id, x_device_token):
            return _error("AUTH", 401)
        if not x_turn_id or not TURN_ID_RE.fullmatch(x_turn_id):
            return _error("BAD_AUDIO", 400)
        if x_sample_rate != "16000":
            return _error("BAD_AUDIO", 400)

        cached = store.get_turn(x_device_id, x_turn_id)
        if cached is not None:
            return cached

        content_length = request.headers.get("content-length")
        if content_length:
            try:
                declared_length = int(content_length)
            except ValueError:
                return _error("BAD_AUDIO", 400)
            if declared_length > MAX_AUDIO_BYTES:
                return _error("TOO_LARGE", 413)

        pcm = await request.body()
        if len(pcm) > MAX_AUDIO_BYTES:
            return _error("TOO_LARGE", 413)
        if len(pcm) < MIN_AUDIO_BYTES or len(pcm) % 2:
            return _error("BAD_AUDIO", 400)
        if provider_lock.locked():
            return _error("BUSY", 429)

        try:
            async with provider_lock:
                turn = await provider.complete_turn(pcm)
        except QwenTimeoutError:
            return _error("TIMEOUT", 504)
        except (QwenProviderError, QwenError):
            return _error("UPSTREAM", 502)

        audio_id = store.put_audio(x_device_id, turn.pcm16)
        body = {
            "turn_id": x_turn_id,
            "transcript": turn.transcript,
            "reply_text": turn.reply_text,
            "action": classify_intent(turn.transcript),
            "audio_id": audio_id,
            "expires_in": store.ttl_seconds,
        }
        store.put_turn(x_device_id, x_turn_id, body)
        return body

    @app.get("/v1/voice/audio/{audio_id}")
    async def voice_audio(
        audio_id: str,
        x_device_id: Optional[str] = Header(default=None),
        x_device_token: Optional[str] = Header(default=None),
    ):
        if not authenticated(x_device_id, x_device_token):
            return _error("AUTH", 401)
        pcm = store.get_audio(audio_id, x_device_id)
        if pcm is None:
            return _error("BAD_AUDIO", 404)
        return Response(pcm, media_type="application/octet-stream")

    return app


def create_app_from_env() -> FastAPI:
    return create_app(Settings.from_env())
