import os
from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "deploy" / "install_voice_gateway.sh"


def bash_command(script, env_file):
    if os.name == "nt" and shutil.which("wsl.exe"):
        def wsl_path(path):
            resolved = Path(path).resolve()
            drive = resolved.drive[0].lower()
            tail = resolved.as_posix().split(":", 1)[1]
            return f"/mnt/{drive}{tail}"

        return [
            "wsl.exe",
            "--exec",
            "bash",
            wsl_path(script),
            "--check-env",
            wsl_path(env_file),
        ]
    command = shutil.which("bash")
    if not command:
        pytest.skip("bash is not installed on this development host")
    return [command, str(script), "--check-env", str(env_file)]


def write_env(path, *, token="a" * 64, api_key="sk-test-key", workspace="ws-test"):
    path.write_text(
        "\n".join(
            [
                f"DASHSCOPE_API_KEY={api_key}",
                f"DASHSCOPE_WORKSPACE_ID={workspace}",
                "EDGI_DEVICE_ID=edgi-talk-01",
                f"EDGI_DEVICE_TOKEN={token}",
                "EDGI_LISTEN_HOST=127.0.0.1",
                "EDGI_LISTEN_PORT=8000",
                "",
            ]
        ),
        encoding="utf-8",
    )


def run_check(path):
    return subprocess.run(
        bash_command(INSTALLER, path),
        cwd=ROOT,
        capture_output=True,
        text=True,
        env={**os.environ, "LC_ALL": "C"},
    )


def test_installer_rejects_unset_secret_without_echoing_other_values(tmp_path):
    env_file = tmp_path / "edgi-voice.env"
    write_env(env_file, api_key="UNSET")

    result = run_check(env_file)

    assert result.returncode != 0
    assert "DASHSCOPE_API_KEY" in result.stderr
    assert "sk-test-key" not in result.stdout + result.stderr
    assert "a" * 64 not in result.stdout + result.stderr


def test_installer_rejects_short_device_token(tmp_path):
    env_file = tmp_path / "edgi-voice.env"
    write_env(env_file, token="short")

    result = run_check(env_file)

    assert result.returncode != 0
    assert "EDGI_DEVICE_TOKEN" in result.stderr


def test_installer_accepts_complete_private_environment(tmp_path):
    env_file = tmp_path / "edgi-voice.env"
    write_env(env_file)

    result = run_check(env_file)

    assert result.returncode == 0
    assert result.stdout.strip() == "environment OK"
    assert result.stderr == ""
