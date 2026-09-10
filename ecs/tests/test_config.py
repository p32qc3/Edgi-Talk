import pytest

from ecs.voice_gateway.config import Settings


REQUIRED = {
    "DASHSCOPE_API_KEY": "sk-test-key",
    "DASHSCOPE_WORKSPACE_ID": "ws-testspace",
    "EDGI_DEVICE_ID": "edgi-talk-01",
    "EDGI_DEVICE_TOKEN": "a" * 64,
}


def set_required(monkeypatch, **overrides):
    values = {**REQUIRED, **overrides}
    for name, value in values.items():
        monkeypatch.setenv(name, value)


def test_settings_loads_required_values_and_defaults(monkeypatch):
    set_required(monkeypatch)

    settings = Settings.from_env()

    assert settings.api_key == "sk-test-key"
    assert settings.workspace_id == "ws-testspace"
    assert settings.device_id == "edgi-talk-01"
    assert settings.device_token == "a" * 64
    assert settings.listen_host == "127.0.0.1"
    assert settings.listen_port == 8000


@pytest.mark.parametrize(
    "missing_name",
    [
        "DASHSCOPE_API_KEY",
        "DASHSCOPE_WORKSPACE_ID",
        "EDGI_DEVICE_ID",
        "EDGI_DEVICE_TOKEN",
    ],
)
def test_settings_rejects_each_missing_required_value(monkeypatch, missing_name):
    set_required(monkeypatch)
    monkeypatch.delenv(missing_name)

    with pytest.raises(ValueError, match=missing_name):
        Settings.from_env()


def test_settings_rejects_short_device_token(monkeypatch):
    set_required(monkeypatch, EDGI_DEVICE_TOKEN="short")

    with pytest.raises(ValueError, match="EDGI_DEVICE_TOKEN"):
        Settings.from_env()


def test_settings_rejects_invalid_listen_port(monkeypatch):
    set_required(monkeypatch)
    monkeypatch.setenv("EDGI_LISTEN_PORT", "70000")

    with pytest.raises(ValueError, match="EDGI_LISTEN_PORT"):
        Settings.from_env()
