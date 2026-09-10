from dataclasses import dataclass

import pytest
from fastapi.testclient import TestClient

from ecs.voice_gateway.app import create_app
from ecs.voice_gateway.config import Settings
from ecs.voice_gateway.qwen_realtime import (
    QwenProviderError,
    QwenTimeoutError,
    QwenTurn,
)


SETTINGS = Settings(
    api_key="sk-test-key",
    workspace_id="ws-testspace",
    device_id="edgi-talk-01",
    device_token="a" * 64,
)


@dataclass
class FakeClock:
    now: float = 100.0

    def __call__(self):
        return self.now


class FakeQwen:
    def __init__(self, error=None):
        self.error = error
        self.calls = []

    async def complete_turn(self, pcm):
        self.calls.append(pcm)
        if self.error:
            raise self.error
        return QwenTurn(
            transcript="查看状态",
            reply_text="小团子现在很开心。",
            pcm16=b"\x02\x00" * 1600,
        )


def headers(turn_id="00000001-00000001", token=None, sample_rate="16000"):
    return {
        "X-Device-ID": "edgi-talk-01",
        "X-Device-Token": token or "a" * 64,
        "X-Turn-ID": turn_id,
        "X-Sample-Rate": sample_rate,
    }


def test_health_endpoint_does_not_require_device_secret():
    client = TestClient(create_app(SETTINGS, qwen=FakeQwen()))

    response = client.get("/healthz")

    assert response.status_code == 200
    assert response.json() == {"status": "ok"}


@pytest.mark.parametrize(
    ("bad_headers", "status"),
    [
        ({}, 401),
        (headers(token="b" * 64), 401),
        ({**headers(), "X-Device-ID": "other-board"}, 401),
    ],
)
def test_turn_rejects_missing_or_wrong_device_auth(bad_headers, status):
    client = TestClient(create_app(SETTINGS, qwen=FakeQwen()))

    response = client.post("/v1/voice/turn", headers=bad_headers, content=b"\0" * 640)

    assert response.status_code == status
    assert response.json() == {"error": "AUTH"}


@pytest.mark.parametrize(
    ("size", "status", "error"),
    [
        (0, 400, "BAD_AUDIO"),
        (639, 400, "BAD_AUDIO"),
        (640, 200, None),
        (256000, 200, None),
        (256001, 413, "TOO_LARGE"),
    ],
)
def test_turn_enforces_audio_size_boundaries(size, status, error):
    client = TestClient(create_app(SETTINGS, qwen=FakeQwen()))

    response = client.post("/v1/voice/turn", headers=headers(), content=b"\0" * size)

    assert response.status_code == status
    if error:
        assert response.json() == {"error": error}


def test_turn_rejects_non_16khz_audio():
    client = TestClient(create_app(SETTINGS, qwen=FakeQwen()))

    response = client.post(
        "/v1/voice/turn", headers=headers(sample_rate="24000"), content=b"\0" * 640
    )

    assert response.status_code == 400
    assert response.json() == {"error": "BAD_AUDIO"}


def test_turn_returns_action_and_authenticated_audio():
    qwen = FakeQwen()
    client = TestClient(create_app(SETTINGS, qwen=qwen))

    response = client.post(
        "/v1/voice/turn", headers=headers(), content=b"\x01\x00" * 320
    )

    assert response.status_code == 200
    body = response.json()
    assert body["turn_id"] == "00000001-00000001"
    assert body["transcript"] == "查看状态"
    assert body["reply_text"] == "小团子现在很开心。"
    assert body["action"] == "show_status"
    assert len(body["audio_id"]) == 32
    assert body["expires_in"] == 60

    audio = client.get(
        f"/v1/voice/audio/{body['audio_id']}", headers=headers()
    )
    assert audio.status_code == 200
    assert audio.headers["content-type"] == "application/octet-stream"
    assert audio.content == b"\x02\x00" * 1600
    assert qwen.calls == [b"\x01\x00" * 320]


def test_duplicate_turn_returns_cached_result_without_second_model_call():
    qwen = FakeQwen()
    client = TestClient(create_app(SETTINGS, qwen=qwen))
    request_headers = headers(turn_id="00000002-00000009")

    first = client.post("/v1/voice/turn", headers=request_headers, content=b"\0" * 640)
    second = client.post("/v1/voice/turn", headers=request_headers, content=b"\1" * 640)

    assert first.status_code == second.status_code == 200
    assert first.json() == second.json()
    assert len(qwen.calls) == 1


def test_audio_expires_after_sixty_seconds():
    clock = FakeClock()
    client = TestClient(create_app(SETTINGS, qwen=FakeQwen(), clock=clock))
    response = client.post("/v1/voice/turn", headers=headers(), content=b"\0" * 640)
    audio_id = response.json()["audio_id"]
    clock.now += 61

    expired = client.get(f"/v1/voice/audio/{audio_id}", headers=headers())

    assert expired.status_code == 404
    assert expired.json() == {"error": "BAD_AUDIO"}


@pytest.mark.parametrize(
    ("error", "status", "code"),
    [
        (QwenTimeoutError("late"), 504, "TIMEOUT"),
        (QwenProviderError("down"), 502, "UPSTREAM"),
    ],
)
def test_turn_maps_provider_failures_to_small_error_codes(error, status, code):
    client = TestClient(create_app(SETTINGS, qwen=FakeQwen(error=error)))

    response = client.post("/v1/voice/turn", headers=headers(), content=b"\0" * 640)

    assert response.status_code == status
    assert response.json() == {"error": code}
