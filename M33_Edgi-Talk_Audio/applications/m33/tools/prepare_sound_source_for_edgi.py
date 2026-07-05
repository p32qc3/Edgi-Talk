# -*- coding: utf-8 -*-
"""
把 workspace/声音源 下演示用 m4a/mp3/wav 转为训练目录 demo_wav/0..4/，
类别与 protocol.h 一致：0 环境 1 火警 2 敲门 3 婴儿 4 水开。

环境类无素材时用与板上特征一致的合成 wav 填充（仅 0/）。

依赖：pip install imageio-ffmpeg numpy
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import wave

import numpy as np

SAMPLE_RATE = 16000
FRAME_SAMPLES = 320

# 文件名关键字 -> 类别（优先匹配更具体的）
NAME_TO_LABEL: list[tuple[str, int]] = [
    ("火灾", 1),
    ("火警", 1),
    ("警报", 1),
    ("fire", 1),
    ("敲门", 2),
    ("knock", 2),
    ("婴儿", 3),
    ("哭泣", 3),
    ("baby", 3),
    ("水沸", 4),
    ("沸腾", 4),
    ("烧水", 4),
    ("boil", 4),
]

CLASS_NAMES = ("环境", "火警", "敲门", "婴儿", "水开")


def find_sound_source_dir(workspace: str) -> str:
    direct = os.path.join(workspace, "声音源")
    if os.path.isdir(direct):
        return direct
    for name in os.listdir(workspace):
        path = os.path.join(workspace, name)
        if not os.path.isdir(path):
            continue
        files = [
            f
            for f in os.listdir(path)
            if f.lower().endswith((".m4a", ".mp3", ".wav", ".flac", ".ogg"))
        ]
        if len(files) >= 4:
            return path
    raise FileNotFoundError("未找到「声音源」目录（需含 4 个演示音频）")


def label_from_filename(name: str) -> int | None:
    base = os.path.splitext(name)[0].lower()
    for key, lab in NAME_TO_LABEL:
        if key.lower() in base or key in name:
            return lab
    return None


def get_ffmpeg() -> str:
    try:
        import imageio_ffmpeg

        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError as e:
        raise RuntimeError("请安装: pip install imageio-ffmpeg") from e


def convert_to_wav16k_mono(ffmpeg: str, src: str, dst: str, volume: float = 1.0) -> None:
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    cmd = [
        ffmpeg,
        "-y",
        "-i",
        src,
        "-ar",
        str(SAMPLE_RATE),
        "-ac",
        "1",
        "-sample_fmt",
        "s16",
    ]
    if abs(volume - 1.0) > 0.01:
        cmd.extend(["-filter:a", f"volume={volume}"])
    cmd.append(dst)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"ffmpeg 失败 {src}:\n{r.stderr}")


def write_wav_i16(path: str, pcm: np.ndarray) -> None:
    pcm = np.clip(pcm, -32768, 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm.tobytes())


def synth_env_wavs(out_dir: str, count: int, seed: int) -> None:
    """与 generate_edgi_mlp_weights 合成环境特征相近的低能量 wav。"""
    rng = np.random.default_rng(seed)
    os.makedirs(out_dir, exist_ok=True)
    for i in range(count):
        n = SAMPLE_RATE * 8
        pcm = rng.integers(-400, 401, size=n, dtype=np.int16)
        write_wav_i16(os.path.join(out_dir, f"synth_env_{i:02d}.wav"), pcm)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--workspace",
        type=str,
        default=None,
        help="含「声音源」的 workspace 根，默认为本脚本上溯 5 级",
    )
    ap.add_argument(
        "--out-root",
        type=str,
        default=None,
        help="输出 demo_wav 根目录（默认 tools/demo_wav）",
    )
    ap.add_argument("--env-synth-count", type=int, default=3, help="合成环境 wav 条数（过多会压过火警/婴儿）")
    ap.add_argument(
        "--augment-gain",
        type=str,
        default="0.85,1.0,1.15",
        help="对 1~4 类各导出多档音量副本，逗号分隔",
    )
    args = ap.parse_args()

    tools_dir = os.path.dirname(os.path.abspath(__file__))
    workspace = args.workspace
    if workspace is None:
        workspace = os.path.normpath(os.path.join(tools_dir, "..", "..", "..", ".."))

    src_dir = find_sound_source_dir(workspace)
    out_root = args.out_root or os.path.join(tools_dir, "demo_wav")
    out_root = os.path.abspath(os.path.expanduser(out_root))

    for i in range(5):
        os.makedirs(os.path.join(out_root, str(i)), exist_ok=True)

    ffmpeg = get_ffmpeg()
    mapped: dict[int, list[str]] = {1: [], 2: [], 3: [], 4: []}

    for fn in sorted(os.listdir(src_dir)):
        if not fn.lower().endswith((".m4a", ".mp3", ".wav", ".flac", ".ogg")):
            continue
        lab = label_from_filename(fn)
        if lab is None:
            print(f"[warn] 无法识别类别，跳过: {fn}")
            continue
        src = os.path.join(src_dir, fn)
        safe = re.sub(r'[<>:"/\\|?*]', "_", os.path.splitext(fn)[0])
        gains = [float(x.strip()) for x in args.augment_gain.split(",") if x.strip()]
        for gi, g in enumerate(gains):
            suffix = "" if gi == 0 else f"_g{gi}"
            dst = os.path.join(out_root, str(lab), f"{safe}{suffix}.wav")
            print(f"  {CLASS_NAMES[lab]} ({lab}) <- {fn} gain={g}")
            convert_to_wav16k_mono(ffmpeg, src, dst, volume=g)
            mapped[lab].append(dst)

    if len(mapped[1]) + len(mapped[2]) + len(mapped[3]) + len(mapped[4]) < 4:
        print("[error] 四类演示音未齐，请检查文件名含：火灾/敲门/婴儿/水沸")
        sys.exit(1)

    env_dir = os.path.join(out_root, "0")
    synth_env_wavs(env_dir, args.env_synth_count, seed=42)
    print(f"[ok] 环境类: 合成 {args.env_synth_count} 条 -> {env_dir}")
    print(f"[ok] 训练数据目录: {out_root}")
    print("下一步:")
    print(
        f"  python generate_edgi_mlp_weights.py --wav-root \"{out_root}\" "
        f"--board-sim --class-weight balanced --emit-centroids"
    )


if __name__ == "__main__":
    main()
