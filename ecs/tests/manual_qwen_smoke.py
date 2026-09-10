from __future__ import annotations

import argparse
import asyncio
import os
from pathlib import Path

from ecs.voice_gateway.config import Settings
from ecs.voice_gateway.qwen_realtime import QwenRealtime


def load_env(path: Path) -> None:
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, value = line.split("=", 1)
        os.environ.setdefault(name, value)


async def run(pcm_path: Path) -> None:
    pcm = pcm_path.read_bytes()
    result = await QwenRealtime(Settings.from_env()).complete_turn(pcm)
    print(f"transcript: {result.transcript}")
    print(f"reply: {result.reply_text}")
    print(f"reply_pcm16_bytes: {len(result.pcm16)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pcm", type=Path)
    parser.add_argument(
        "--env", type=Path, default=Path("/etc/edgi-voice/edgi-voice.env")
    )
    args = parser.parse_args()
    load_env(args.env)
    asyncio.run(run(args.pcm))


if __name__ == "__main__":
    main()
