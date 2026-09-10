from dataclasses import dataclass
import os


_REQUIRED_ENV = (
    "DASHSCOPE_API_KEY",
    "DASHSCOPE_WORKSPACE_ID",
    "EDGI_DEVICE_ID",
    "EDGI_DEVICE_TOKEN",
)


def _required(name: str) -> str:
    value = os.environ.get(name, "").strip()
    if not value:
        raise ValueError(f"{name} is required")
    return value


@dataclass(frozen=True)
class Settings:
    api_key: str
    workspace_id: str
    device_id: str
    device_token: str
    listen_host: str = "127.0.0.1"
    listen_port: int = 8000

    @classmethod
    def from_env(cls) -> "Settings":
        values = {name: _required(name) for name in _REQUIRED_ENV}
        token = values["EDGI_DEVICE_TOKEN"]
        if len(token) < 32:
            raise ValueError("EDGI_DEVICE_TOKEN must contain at least 32 characters")

        port_text = os.environ.get("EDGI_LISTEN_PORT", "8000").strip()
        try:
            port = int(port_text)
        except ValueError as exc:
            raise ValueError("EDGI_LISTEN_PORT must be an integer") from exc
        if port < 1 or port > 65535:
            raise ValueError("EDGI_LISTEN_PORT must be between 1 and 65535")

        return cls(
            api_key=values["DASHSCOPE_API_KEY"],
            workspace_id=values["DASHSCOPE_WORKSPACE_ID"],
            device_id=values["EDGI_DEVICE_ID"],
            device_token=token,
            listen_host=os.environ.get("EDGI_LISTEN_HOST", "127.0.0.1").strip()
            or "127.0.0.1",
            listen_port=port,
        )
