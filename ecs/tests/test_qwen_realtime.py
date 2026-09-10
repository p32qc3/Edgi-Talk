import asyncio
import base64
import json

import pytest

from ecs.voice_gateway.config import Settings
from ecs.voice_gateway.qwen_realtime import (
    QwenProtocolError,
    QwenProviderError,
    QwenRealtime,
    QwenTimeoutError,
)


SETTINGS = Settings(
    api_key="sk-test-key",
    workspace_id="ws-testspace",
    device_id="edgi-talk-01",
    device_token="a" * 64,
)


class FakeSocket:
    def __init__(self, events, delay_forever=False):
        self.events = iter(events)
        self.sent = []
        self.delay_forever = delay_forever

    async def send(self, message):
        self.sent.append(json.loads(message))

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.delay_forever:
            await asyncio.Future()
        try:
            return json.dumps(next(self.events))
        except StopIteration as exc:
            raise StopAsyncIteration from exc


class FakeConnect:
    def __init__(self, socket):
        self.socket = socket
        self.url = None
        self.headers = None

    def __call__(self, url, *, additional_headers):
        self.url = url
        self.headers = additional_headers
        socket = self.socket

        class Connection:
            async def __aenter__(self):
                return socket

            async def __aexit__(self, exc_type, exc, tb):
                return False

        return Connection()


def successful_events(audio24):
    middle = len(audio24) // 2
    return [
        {
            "type": "conversation.item.input_audio_transcription.completed",
            "transcript": "查看状态",
        },
        {
            "type": "response.audio.delta",
            "delta": base64.b64encode(audio24[:middle]).decode("ascii"),
        },
        {
            "type": "response.audio.delta",
            "delta": base64.b64encode(audio24[middle:]).decode("ascii"),
        },
        {
            "type": "response.audio_transcript.done",
            "transcript": "小团子现在很开心。",
        },
        {"type": "response.done", "response": {"status": "completed"}},
    ]


def test_complete_turn_sends_push_to_talk_sequence_and_resamples_reply():
    input_pcm = b"\x01\x00" * 3200
    audio24 = b"\x02\x00" * 24000
    socket = FakeSocket(successful_events(audio24))
    connect = FakeConnect(socket)
    client = QwenRealtime(SETTINGS, connect=connect)

    result = asyncio.run(client.complete_turn(input_pcm))

    assert result.transcript == "查看状态"
    assert result.reply_text == "小团子现在很开心。"
    assert 31998 <= len(result.pcm16) <= 32002
    assert connect.url == (
        "wss://ws-testspace.cn-beijing.maas.aliyuncs.com/"
        "api-ws/v1/realtime?model=qwen-audio-3.0-realtime-flash"
    )
    assert connect.headers == {"Authorization": "Bearer sk-test-key"}
    assert socket.sent[0]["type"] == "session.update"
    assert socket.sent[0]["session"]["turn_detection"] is None
    appends = [message for message in socket.sent if message["type"] == "input_audio_buffer.append"]
    assert [len(base64.b64decode(message["audio"])) for message in appends] == [3200, 3200]
    assert [message["type"] for message in socket.sent[-2:]] == [
        "input_audio_buffer.commit",
        "response.create",
    ]


def test_complete_turn_rejects_provider_error_event():
    socket = FakeSocket(
        [{"type": "error", "error": {"code": "invalid_value", "message": "bad"}}]
    )
    client = QwenRealtime(SETTINGS, connect=FakeConnect(socket))

    with pytest.raises(QwenProviderError, match="invalid_value"):
        asyncio.run(client.complete_turn(b"\0" * 640))


def test_complete_turn_rejects_malformed_audio_delta():
    socket = FakeSocket(
        [
            {"type": "response.audio.delta", "delta": "%%%not-base64%%%"},
            {"type": "response.done", "response": {"status": "completed"}},
        ]
    )
    client = QwenRealtime(SETTINGS, connect=FakeConnect(socket))

    with pytest.raises(QwenProtocolError, match="audio"):
        asyncio.run(client.complete_turn(b"\0" * 640))


def test_complete_turn_rejects_failed_response():
    socket = FakeSocket(
        [{"type": "response.done", "response": {"status": "failed"}}]
    )
    client = QwenRealtime(SETTINGS, connect=FakeConnect(socket))

    with pytest.raises(QwenProviderError, match="failed"):
        asyncio.run(client.complete_turn(b"\0" * 640))


def test_complete_turn_times_out_when_provider_never_replies():
    socket = FakeSocket([], delay_forever=True)
    client = QwenRealtime(SETTINGS, connect=FakeConnect(socket), timeout_seconds=0.01)

    with pytest.raises(QwenTimeoutError):
        asyncio.run(client.complete_turn(b"\0" * 640))
