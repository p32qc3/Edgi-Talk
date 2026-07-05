# -*- coding: utf-8 -*-
"""
训练与 M33 上 app_ai.c / app_ai_tflm_mlp.c 完全一致的特征：
  - 每帧 320 点 int16 @16kHz mono
  - pcm_to_spectrogram_stub → 40×40 uint8
  - 每行 40 像素求和 / (40*255) → 40 维 float（与板上 edgi_row_means 一致）

类别（必须与 protocol.h 顺序一致，共 5 类）：
  0 环境  1 火警  2 敲门  3 婴儿  4 水开

用法：
  1) 合成数据（仅验证链路）：
       python generate_edgi_mlp_weights.py

  2) 真实 wav（推荐）：目录结构
       data_root/0/*.wav  环境
       data_root/1/*.wav  火警
       …
       data_root/4/*.wav  水开
     要求：16-bit PCM；单声道优先；采样率 16kHz（否则脚本做简单重采样）
       python generate_edgi_mlp_weights.py --wav-root H:/my_data

  3) 已有特征矩阵：
       np.savez("d.npz", X=..., y=...)  # X: (N,40) float64, y: (N,) int 0..4
       python generate_edgi_mlp_weights.py --npz H:/d.npz
"""
from __future__ import annotations

import argparse
import os
import wave

import numpy as np
from sklearn.neural_network import MLPClassifier
from sklearn.model_selection import train_test_split

# 与 edgi_audio_capture.h 一致
SAMPLE_RATE = 16000
FRAME_SAMPLES = 320
IN_DIM = 40
HIDDEN = 48
N_CLASSES = 5

CLASS_NAMES_CN = ("环境", "火警", "敲门", "婴儿", "水开")

# 与 edgi_audio_capture.h 中 EDGI_SPEC_ENERGY_SHIFT 一致
SPEC_ENERGY_SHIFT = 13


def pcm_to_spec_uint8(pcm_i16: np.ndarray) -> np.ndarray:
    """与 app_ai.c 中 pcm_to_spectrogram_stub 逐位一致。"""
    pcm = pcm_i16[:FRAME_SAMPLES].astype(np.int32, copy=False)
    out = np.zeros((40, 40), dtype=np.uint8)
    for r in range(40):
        base = (r * FRAME_SAMPLES) // 40
        acc = 0
        for k in range(8):
            if base + k >= FRAME_SAMPLES:
                break
            v = int(pcm[base + k])
            acc += v * v
        e = acc >> SPEC_ENERGY_SHIFT
        if e > 255:
            e = 255
        out[r, :] = np.uint8(e)
    return out


def spec_to_features(spec: np.ndarray) -> np.ndarray:
    """与 edgi_spec_features.c：行均值 + 一阶差分 一致。"""
    row = spec.sum(axis=1).astype(np.float64) / (40.0 * 255.0)
    out = np.empty_like(row)
    prev = 0.0
    for i in range(40):
        out[i] = row[i] - prev
        prev = row[i]
    return out


def simple_resample_mono(x: np.ndarray, src_rate: int, dst_rate: int) -> np.ndarray:
    if src_rate == dst_rate:
        return x
    x = x.astype(np.float64, copy=False)
    ratio = dst_rate / float(src_rate)
    n_out = max(1, int(round(len(x) * ratio)))
    idx = (np.arange(n_out, dtype=np.float64) / ratio).astype(np.int64)
    idx = np.clip(idx, 0, len(x) - 1)
    y = x[idx]
    return np.clip(np.round(y), -32768, 32767).astype(np.int16)


def read_wav_mono_i16(path: str) -> tuple[np.ndarray, int]:
    with wave.open(path, "rb") as w:
        nch = w.getnchannels()
        sw = w.getsampwidth()
        fr = w.getframerate()
        nframes = w.getnframes()
        raw = w.readframes(nframes)
    if sw != 2:
        raise ValueError(f"{path}: 需要 16-bit PCM，当前 {sw * 8}-bit")
    samples = np.frombuffer(raw, dtype="<i2")
    if nch == 2:
        samples = samples.reshape(-1, 2).mean(axis=1)
        samples = np.clip(samples, -32768, 32767).astype(np.int16)
    elif nch != 1:
        raise ValueError(f"{path}: 需要单/双声道，当前 {nch}")
    return samples, fr


def iter_frames_from_pcm(pcm: np.ndarray, hop: int) -> list[np.ndarray]:
    if pcm.size < FRAME_SAMPLES:
        return []
    out = []
    for start in range(0, pcm.size - FRAME_SAMPLES + 1, hop):
        out.append(pcm[start : start + FRAME_SAMPLES].copy())
    return out


def load_synthetic(n_samples: int, seed: int) -> tuple[np.ndarray, np.ndarray]:
    rng = np.random.default_rng(seed)
    X = np.zeros((n_samples, IN_DIM), dtype=np.float64)
    Y = np.zeros(n_samples, dtype=np.int64)

    def synth_spec_for_label(y: int) -> np.ndarray:
        if y == 0:
            mean_cell = rng.uniform(0.0, 48.0)
        elif y == 1:
            mean_cell = rng.uniform(78.0, 112.0)
        elif y == 2:
            mean_cell = rng.uniform(52.0, 74.0)
        elif y == 3:
            mean_cell = rng.uniform(118.0, 248.0)
        else:
            mean_cell = rng.uniform(60.0, 95.0)
        spec = rng.normal(mean_cell, 18.0, size=(40, 40))
        return np.clip(spec, 0.0, 255.0).astype(np.uint8)

    for i in range(n_samples):
        y = int(rng.integers(0, N_CLASSES))
        spec = synth_spec_for_label(y)
        X[i] = spec_to_features(spec)
        Y[i] = y
    return X, Y


def load_from_npz(path: str) -> tuple[np.ndarray, np.ndarray]:
    z = np.load(path, allow_pickle=False)
    if "X" not in z or "y" not in z:
        raise KeyError("npz 需包含数组 X (N,40) 与 y (N,)")
    X = np.asarray(z["X"], dtype=np.float64)
    y = np.asarray(z["y"], dtype=np.int64).ravel()
    if X.ndim != 2 or X.shape[1] != IN_DIM:
        raise ValueError(f"X 形状应为 (N,{IN_DIM})，当前 {X.shape}")
    if y.shape[0] != X.shape[0]:
        raise ValueError("X、y 行数不一致")
    if y.min() < 0 or y.max() >= N_CLASSES:
        raise ValueError(f"标签需在 0..{N_CLASSES-1}")
    return X, y


def augment_pcm_board(pcm: np.ndarray, gain: float, rng: np.random.Generator) -> np.ndarray:
    """模拟手机外放 + MIC 增益：幅度缩放 + 底噪。"""
    y = pcm.astype(np.float64) * gain
    y += rng.integers(-700, 701, size=len(y))
    return np.clip(np.round(y), -32768, 32767).astype(np.int16)


def load_from_wav_root(
    root: str, hop: int, board_sim: bool = False, seed: int = 42
) -> tuple[np.ndarray, np.ndarray]:
    xs: list[np.ndarray] = []
    ys: list[int] = []
    rng = np.random.default_rng(seed)
    gains = [0.35, 0.55, 0.85, 1.0, 1.6, 2.5, 4.0] if board_sim else [1.0]
    for label in range(N_CLASSES):
        sub = os.path.join(root, str(label))
        if not os.path.isdir(sub):
            print(f"[warn] 缺少目录（将跳过该类）: {sub}")
            continue
        for fn in sorted(os.listdir(sub)):
            if not fn.lower().endswith(".wav"):
                continue
            path = os.path.join(sub, fn)
            try:
                pcm, fr = read_wav_mono_i16(path)
            except Exception as e:
                print(f"[skip] {path}: {e}")
                continue
            if fr != SAMPLE_RATE:
                pcm = simple_resample_mono(pcm, fr, SAMPLE_RATE)
            frames = iter_frames_from_pcm(pcm, hop)
            n_feat = 0
            for frame in frames:
                for g in gains:
                    aug = augment_pcm_board(frame, g, rng) if board_sim else frame
                    spec = pcm_to_spec_uint8(aug)
                    xs.append(spec_to_features(spec))
                    ys.append(label)
                    n_feat += 1
            print(f"  label {label} ({CLASS_NAMES_CN[label]}): {fn} -> +{n_feat} 帧")
    if not xs:
        raise RuntimeError(f"在 {root} 下未读到任何 wav 帧，请检查目录与格式")
    return np.stack(xs, axis=0), np.asarray(ys, dtype=np.int64)


def balance_frames_per_class(
    X: np.ndarray, Y: np.ndarray, target_per_class: int, seed: int
) -> tuple[np.ndarray, np.ndarray]:
    """演示音源帧数悬殊时，每类重采样到相近帧数再训练。"""
    rng = np.random.default_rng(seed)
    xs: list[np.ndarray] = []
    ys: list[int] = []
    for label in range(N_CLASSES):
        idx = np.flatnonzero(Y == label)
        if idx.size == 0:
            continue
        if idx.size >= target_per_class:
            pick = rng.choice(idx, size=target_per_class, replace=False)
        else:
            pick = rng.choice(idx, size=target_per_class, replace=True)
        xs.append(X[pick])
        ys.extend([label] * target_per_class)
    return np.vstack(xs), np.asarray(ys, dtype=np.int64)


def train_and_emit(
    X: np.ndarray,
    Y: np.ndarray,
    out_path: str,
    seed: int,
    test_size: float,
    class_weight: str | None = None,
) -> None:
    if class_weight == "balanced":
        counts = [int((Y == i).sum()) for i in range(N_CLASSES) if (Y == i).sum() > 0]
        target = min(max(counts), 2500) if counts else X.shape[0]
        X, Y = balance_frames_per_class(X, Y, target, seed)
        print("balanced 每类帧数 ->", target, "总", X.shape[0])

    if test_size > 0 and X.shape[0] >= 10:
        try:
            X_train, X_test, y_train, y_test = train_test_split(
                X, Y, test_size=test_size, random_state=seed, stratify=Y
            )
        except ValueError:
            X_train, X_test, y_train, y_test = train_test_split(
                X, Y, test_size=test_size, random_state=seed
            )
    else:
        X_train, y_train = X, Y
        X_test, y_test = None, None

    clf = MLPClassifier(
        hidden_layer_sizes=(HIDDEN,),
        activation="relu",
        solver="adam",
        max_iter=3000,
        random_state=seed,
        early_stopping=False,
        tol=1e-5,
    )
    clf.fit(X_train, y_train)
    print("train acc:", clf.score(X_train, y_train))
    if X_test is not None:
        print("test acc: ", clf.score(X_test, y_test))

    W1 = np.ascontiguousarray(clf.coefs_[0], dtype=np.float64)
    b1 = np.ascontiguousarray(clf.intercepts_[0], dtype=np.float64)
    W2 = np.ascontiguousarray(clf.coefs_[1], dtype=np.float64)
    b2 = np.ascontiguousarray(clf.intercepts_[1], dtype=np.float64)

    assert W1.shape == (IN_DIM, HIDDEN)
    assert b1.shape == (HIDDEN,)
    assert W2.shape == (HIDDEN, N_CLASSES)
    assert b2.shape == (N_CLASSES,)

    def emit_arr(name, arr):
        flat = arr.astype(np.float32).ravel()
        parts = ", ".join(f"{x:.9e}f" for x in flat)
        return f"static const float {name}[{flat.size}] = {{ {parts} }};\n"

    with open(out_path, "w", encoding="utf-8") as f:
        f.write(
            "/* Auto-generated by tools/generate_edgi_mlp_weights.py — do not edit by hand. */\n"
        )
        f.write("#ifndef EDGI_MLP_WEIGHTS_H\n#define EDGI_MLP_WEIGHTS_H\n\n")
        f.write(f"#define EDGI_MLP_IN_DIM   {IN_DIM}u\n")
        f.write(f"#define EDGI_MLP_HIDDEN   {HIDDEN}u\n")
        f.write(f"#define EDGI_MLP_OUT_DIM  {N_CLASSES}u\n\n")
        f.write(emit_arr("edgi_mlp_w1", W1))
        f.write(emit_arr("edgi_mlp_b1", b1))
        f.write(emit_arr("edgi_mlp_w2", W2))
        f.write(emit_arr("edgi_mlp_b2", b2))
        f.write("\n#endif /* EDGI_MLP_WEIGHTS_H */\n")
    print("wrote", out_path)


def emit_demo_templates(wav_root: str, out_path: str, seed: int = 42) -> None:
    """每类用声音源主文件多档增益特征的中位数，板上播同文件时最稳。"""
    rng = np.random.default_rng(seed)
    # 过高增益会把模板顶到 ~1.0，板上稍响就全判火警
    gains = [0.65, 0.85, 1.0, 1.15, 1.35]
    tpl = np.zeros((4, IN_DIM), dtype=np.float64)
    names_cn = CLASS_NAMES_CN[1:5]

    for label in range(1, N_CLASSES):
        sub = os.path.join(wav_root, str(label))
        if not os.path.isdir(sub):
            raise FileNotFoundError(sub)
        prim = [
            f
            for f in sorted(os.listdir(sub))
            if f.lower().endswith(".wav") and "_g" not in f
        ]
        if not prim:
            raise FileNotFoundError(f"no primary wav in {sub}")
        pcm, fr = read_wav_mono_i16(os.path.join(sub, prim[0]))
        if fr != SAMPLE_RATE:
            pcm = simple_resample_mono(pcm, fr, SAMPLE_RATE)
        feats: list[np.ndarray] = []
        for frame in iter_frames_from_pcm(pcm, 160):
            for g in gains:
                aug = augment_pcm_board(frame, g, rng)
                feats.append(spec_to_features(pcm_to_spec_uint8(aug)))
        tpl[label - 1] = np.median(np.stack(feats, axis=0), axis=0)

    lines = [
        "/* Auto-generated: 声音源四类模板 (火灾/敲门/婴儿/水沸) */",
        "#ifndef EDGI_DEMO_TEMPLATES_H",
        "#define EDGI_DEMO_TEMPLATES_H\n",
        f"#define EDGI_DEMO_TPL_DIM {IN_DIM}u\n",
        "#define EDGI_DEMO_TPL_COUNT 4u\n",
        "static const float edgi_demo_tpl[EDGI_DEMO_TPL_COUNT][EDGI_DEMO_TPL_DIM] = {\n",
    ]
    for i in range(4):
        row = ", ".join(f"{v:.8e}f" for v in tpl[i])
        lines.append(f"  /* {names_cn[i]} */ {{ {row} }},\n")
    lines.append("};\n\n#endif /* EDGI_DEMO_TEMPLATES_H */\n")
    with open(out_path, "w", encoding="utf-8") as f:
        f.writelines(lines)
    print("wrote", out_path)


def emit_demo_centroids(X: np.ndarray, Y: np.ndarray, out_path: str) -> None:
    cent = np.zeros((N_CLASSES, IN_DIM), dtype=np.float64)
    for label in range(N_CLASSES):
        idx = Y == label
        if np.any(idx):
            cent[label] = X[idx].mean(axis=0)
    lines = [
        "/* Auto-generated: 声音源特征质心，供 app_ai_demo_centroid.c 最近邻推理 */",
        "#ifndef EDGI_DEMO_CENTROIDS_H",
        "#define EDGI_DEMO_CENTROIDS_H\n",
        f"#define EDGI_DEMO_CENTROID_DIM {IN_DIM}u\n",
        f"#define EDGI_DEMO_CENTROID_CLASSES {N_CLASSES}u\n",
        "static const float edgi_demo_centroid[EDGI_DEMO_CENTROID_CLASSES][EDGI_DEMO_CENTROID_DIM] = {\n",
    ]
    for label in range(N_CLASSES):
        row = ", ".join(f"{v:.8e}f" for v in cent[label])
        lines.append(f"  /* {CLASS_NAMES_CN[label]} */ {{ {row} }},\n")
    lines.append("};\n\n#endif /* EDGI_DEMO_CENTROIDS_H */\n")
    with open(out_path, "w", encoding="utf-8") as f:
        f.writelines(lines)
    print("wrote", out_path)


def main():
    ap = argparse.ArgumentParser(description="生成 edgi_mlp_weights.h")
    ap.add_argument(
        "--wav-root",
        type=str,
        default=None,
        help="数据根目录，下含 0/ 1/ … 4/ 子文件夹存放 wav",
    )
    ap.add_argument(
        "--npz",
        type=str,
        default=None,
        help="npz：键 X (N,40), y (N,)",
    )
    ap.add_argument(
        "--synthetic-samples",
        type=int,
        default=32000,
        help="无真实数据时合成样本数（默认 32000）",
    )
    ap.add_argument("--hop", type=int, default=160, help="wav 滑窗步长（样本），默认 160=50%% 重叠")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--test-size", type=float, default=0.15)
    ap.add_argument(
        "--class-weight",
        type=str,
        default="none",
        choices=("none", "balanced"),
        help="balanced 可缓解某类帧数过少（如演示仅 4 条音源）",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=str,
        default=None,
        help="输出 edgi_mlp_weights.h 路径（默认本包 m33/include/）",
    )
    ap.add_argument(
        "--board-sim",
        action="store_true",
        help="PCM 多档增益+底噪，贴近手机外放经 MIC 采集",
    )
    ap.add_argument(
        "--emit-centroids",
        action="store_true",
        help="同时生成 edgi_demo_centroids.h（演示四类质心推理）",
    )
    args = ap.parse_args()

    out_path = args.output
    include_dir = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "include"))
    if out_path is None:
        out_path = os.path.join(include_dir, "edgi_mlp_weights.h")

    if args.npz:
        X, Y = load_from_npz(args.npz)
        print("npz:", X.shape, "labels:", [int((Y == i).sum()) for i in range(N_CLASSES)])
    elif args.wav_root:
        X, Y = load_from_wav_root(
            os.path.expanduser(args.wav_root),
            hop=args.hop,
            board_sim=args.board_sim,
            seed=args.seed,
        )
        print("wav 特征:", X.shape, "每类帧数:", [int((Y == i).sum()) for i in range(N_CLASSES)])
    else:
        X, Y = load_synthetic(args.synthetic_samples, args.seed)
        print("synthetic:", X.shape)

    if args.emit_centroids:
        emit_demo_centroids(X, Y, os.path.join(include_dir, "edgi_demo_centroids.h"))
        if args.wav_root:
            emit_demo_templates(
                os.path.expanduser(args.wav_root),
                os.path.join(include_dir, "edgi_demo_templates.h"),
                seed=args.seed,
            )

    train_and_emit(
        X,
        Y,
        out_path,
        seed=args.seed,
        test_size=args.test_size,
        class_weight=args.class_weight,
    )


if __name__ == "__main__":
    main()
