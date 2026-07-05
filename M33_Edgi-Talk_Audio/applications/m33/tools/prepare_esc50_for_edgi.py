# -*- coding: utf-8 -*-
"""
把 ESC-50 解压后的文件，按「赛题 5 类」复制到 wav-root 所需的 0/1/2/3/4/ 目录。

ESC-50 结构说明（避免和 fold 混淆）：
  - 音频通常在：ESC-50-master/audio/*.wav
  - 标签表在：ESC-50-master/meta/esc50.csv
  - CSV 列：filename, fold, target, category, ...
  - fold（1~5）：交叉验证折，**不是**类别；同一类声音会分散在不同 fold。
  - category：50 个细类英文名；本脚本把细类 **映射** 到你的 0~4。

用法示例：
  python prepare_esc50_for_edgi.py ^
    --esc50-root "D:/datasets/ESC-50-master" ^
    --out-root "D:/edgi_wav"

再训练：
  python generate_edgi_mlp_weights.py --wav-root D:/edgi_wav
"""
from __future__ import annotations

import argparse
import csv
import os
import shutil

# ESC-50 category 字符串 -> 你的 protocol 类别 0..4
# （近似映射，水开/火警在 ESC-50 里没有完全对应物，仅用相近声音）
ESC_CATEGORY_TO_EDGI: dict[str, int] = {
    # 0 环境：自然声 + 偏背景的人声/室内细碎声
    "rain": 0,
    "sea_waves": 0,
    "wind": 0,
    "crickets": 0,
    "chirping_birds": 0,
    "water_drops": 0,
    "thunderstorm": 0,
    "breathing": 0,
    "footsteps": 0,
    "mouse_click": 0,
    "keyboard_typing": 0,
    "clock_tick": 0,
    "brushing_teeth": 0,
    "snoring": 0,
    "sneezing": 0,
    "insects": 0,
    # 1 火警（近似）：警报、警笛、明火声
    "siren": 1,
    "clock_alarm": 1,
    "church_bells": 1,
    "fireworks": 1,
    "crackling_fire": 1,
    # 2 敲门
    "door_wood_knock": 2,
    # 3 婴儿
    "crying_baby": 3,
    # 4 水开（近似）：倒水、冲水、啜饮等与水相关
    "pouring_water": 4,
    "toilet_flush": 4,
    "drinking_sipping": 4,
}

CLASS_NAMES = ("环境", "火警", "敲门", "婴儿", "水开")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--esc50-root",
        required=True,
        help="ESC-50 解压根目录（内含 audio/ 与 meta/esc50.csv）",
    )
    ap.add_argument(
        "--out-root",
        required=True,
        help="输出根目录，将创建 0/ 1/ 2/ 3/ 4/",
    )
    ap.add_argument(
        "--try-hardlink",
        action="store_true",
        help="优先硬链接省空间（同盘）；失败则自动改为复制。默认直接复制。",
    )
    args = ap.parse_args()

    esc_root = os.path.abspath(os.path.expanduser(args.esc50_root))
    audio_dir = os.path.join(esc_root, "audio")
    csv_path = os.path.join(esc_root, "meta", "esc50.csv")
    out_root = os.path.abspath(os.path.expanduser(args.out_root))

    if not os.path.isdir(audio_dir):
        raise FileNotFoundError(f"找不到 audio 目录: {audio_dir}")
    if not os.path.isfile(csv_path):
        raise FileNotFoundError(f"找不到 esc50.csv: {csv_path}")

    for i in range(5):
        os.makedirs(os.path.join(out_root, str(i)), exist_ok=True)

    counts = [0, 0, 0, 0, 0]
    skipped = 0
    missing = 0

    with open(csv_path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            fn = row["filename"].strip()
            cat = row["category"].strip()
            if cat not in ESC_CATEGORY_TO_EDGI:
                skipped += 1
                continue
            edgi = ESC_CATEGORY_TO_EDGI[cat]
            src = os.path.join(audio_dir, fn)
            if not os.path.isfile(src):
                missing += 1
                continue
            dst = os.path.join(out_root, str(edgi), fn)
            if os.path.exists(dst):
                continue
            if args.try_hardlink:
                try:
                    os.link(src, dst)
                except OSError:
                    shutil.copy2(src, dst)
            else:
                shutil.copy2(src, dst)
            counts[edgi] += 1

    print("已写入:", out_root)
    for i in range(5):
        print(f"  [{i}] {CLASS_NAMES[i]}: {counts[i]} 个文件")
    print(f"未映射的 ESC 类别行（已跳过）: {skipped}（其余 50 类里未出现在映射表中的）")
    if missing:
        print(f"警告: CSV 中有但 audio 里缺失: {missing}")
    print("\n下一步: python generate_edgi_mlp_weights.py --wav-root", out_root)


if __name__ == "__main__":
    main()
