# -*- coding: utf-8 -*-
"""
基于 TensorFlow Lite Micro Speech 同款思路（Apache-2.0）：
  16 kHz mono → log-mel 49×40 → 轻量 2D CNN → 5 类 softmax

类别（与 protocol.h 一致）：
  0 环境  1 火警  2 敲门  3 婴儿  4 水开

依赖：pip install numpy tensorflow scipy

用法：
  cd applications/m33/tools
  python prepare_sound_source_for_edgi.py   # 可选，更新 demo_wav
  python train_edgi_open_cnn.py --wav-root demo_wav

输出（自动写入 ../include/）：
  edgi_mel_config.h / edgi_mel_filterbank.h / edgi_open_cnn_weights.h
"""
from __future__ import annotations

import argparse
import os
import struct
import wave
from typing import Iterable

import numpy as np

SAMPLE_RATE = 16000
N_CLASSES = 5
CLASS_NAMES = ("环境", "火警", "敲门", "婴儿", "水开")

# 与 micro_speech 接近：30 ms 窗、20 ms 步、约 1 s 音频 → 49 帧
WIN_MS = 30
HOP_MS = 20
CLIP_MS = 1000
N_FFT = 512
N_MELS = 40
N_FRAMES = 49

WIN_SAMPLES = SAMPLE_RATE * WIN_MS // 1000
HOP_SAMPLES = SAMPLE_RATE * HOP_MS // 1000
CLIP_SAMPLES = SAMPLE_RATE * CLIP_MS // 1000


def read_wav_mono_i16(path: str) -> tuple[np.ndarray, int]:
    with wave.open(path, "rb") as w:
        nch = w.getnchannels()
        sw = w.getsampwidth()
        fr = w.getframerate()
        raw = w.readframes(w.getnframes())
    if sw != 2:
        raise ValueError(f"{path}: need 16-bit PCM")
    x = np.frombuffer(raw, dtype="<i2")
    if nch == 2:
        x = x.reshape(-1, 2).mean(axis=1).astype(np.int16)
    return x, fr


def resample_if_needed(x: np.ndarray, src: int, dst: int) -> np.ndarray:
    if src == dst:
        return x
    ratio = dst / float(src)
    n_out = max(1, int(round(len(x) * ratio)))
    idx = (np.arange(n_out) / ratio).astype(np.int64)
    idx = np.clip(idx, 0, len(x) - 1)
    y = x[idx].astype(np.float64)
    return np.clip(np.round(y), -32768, 32767).astype(np.int16)


def hz_to_mel(f: np.ndarray) -> np.ndarray:
    return 2595.0 * np.log10(1.0 + f / 700.0)


def mel_to_hz(m: np.ndarray) -> np.ndarray:
    return 700.0 * (10.0 ** (m / 2595.0) - 1.0)


def build_mel_filterbank() -> tuple[np.ndarray, np.ndarray]:
    fmax = SAMPLE_RATE / 2.0
    mel_min = hz_to_mel(np.array([0.0]))[0]
    mel_max = hz_to_mel(np.array([fmax]))[0]
    mels = np.linspace(mel_min, mel_max, N_MELS + 2)
    hz = mel_to_hz(mels)
    bins = np.floor((N_FFT + 1) * hz / SAMPLE_RATE).astype(np.int32)
    fb = np.zeros((N_MELS, N_FFT // 2 + 1), dtype=np.float64)
    for i in range(N_MELS):
        left, center, right = bins[i], bins[i + 1], bins[i + 2]
        if center == left:
            center += 1
        if right == center:
            right += 1
        for k in range(left, center):
            if 0 <= k < fb.shape[1]:
                fb[i, k] = (k - left) / max(1, center - left)
        for k in range(center, right):
            if 0 <= k < fb.shape[1]:
                fb[i, k] = (right - k) / max(1, right - center)
    hann = np.hanning(WIN_SAMPLES).astype(np.float32)
    return fb.astype(np.float32), hann


def pcm_clip_to_mel(pcm_i16: np.ndarray, fb: np.ndarray, hann: np.ndarray) -> np.ndarray:
    x = pcm_i16[:CLIP_SAMPLES].astype(np.float32)
    if x.size < CLIP_SAMPLES:
        x = np.concatenate([x, np.zeros(CLIP_SAMPLES - x.size, dtype=np.float32)])
    x = x / 32768.0
    frames = []
    pos = 0
    while pos + WIN_SAMPLES <= CLIP_SAMPLES and len(frames) < N_FRAMES:
        seg = x[pos : pos + WIN_SAMPLES] * hann
        spec = np.fft.rfft(seg, n=N_FFT)
        power = (spec.real ** 2 + spec.imag ** 2).astype(np.float64)
        mel = np.log(fb @ power + 1e-6).astype(np.float32)
        frames.append(mel)
        pos += HOP_SAMPLES
    if not frames:
        return np.zeros((N_FRAMES, N_MELS), dtype=np.float32)
    out = np.stack(frames, axis=0)
    if out.shape[0] < N_FRAMES:
        out = np.concatenate([out, np.zeros((N_FRAMES - out.shape[0], N_MELS), np.float32)], axis=0)
    out = out[:N_FRAMES]
    # A speaker-to-microphone path changes the overall level far more than the
    # spectral shape. Remove that clip-wide offset so inference is insensitive
    # to ordinary playback volume and distance changes.
    out -= np.mean(out, dtype=np.float64).astype(np.float32)
    return out


def iter_clips(root: str, hop_ms: int = 500) -> Iterable[tuple[np.ndarray, int]]:
    hop = SAMPLE_RATE * hop_ms // 1000
    for label in range(N_CLASSES):
        sub = os.path.join(root, str(label))
        if not os.path.isdir(sub):
            continue
        for fn in sorted(os.listdir(sub)):
            if not fn.lower().endswith(".wav"):
                continue
            pcm, fr = read_wav_mono_i16(os.path.join(sub, fn))
            pcm = resample_if_needed(pcm, fr, SAMPLE_RATE)
            if pcm.size < CLIP_SAMPLES:
                continue
            for start in range(0, pcm.size - CLIP_SAMPLES + 1, hop):
                yield pcm[start : start + CLIP_SAMPLES], label


def load_dataset(root: str, fb: np.ndarray, hann: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    xs, ys = [], []
    for pcm, y in iter_clips(root):
        xs.append(pcm_clip_to_mel(pcm, fb, hann))
        ys.append(y)
    if not xs:
        raise FileNotFoundError(f"no wav clips under {root}/0..4")
    return np.stack(xs).astype(np.float32), np.array(ys, dtype=np.int64)


def build_keras_model():
    import tensorflow as tf

    inp = tf.keras.Input(shape=(N_FRAMES, N_MELS, 1), name="mel")
    x = tf.keras.layers.Conv2D(8, (5, 5), padding="same", activation="relu")(inp)
    x = tf.keras.layers.MaxPooling2D((4, 4))(x)
    x = tf.keras.layers.Conv2D(16, (3, 3), padding="same", activation="relu")(x)
    x = tf.keras.layers.GlobalAveragePooling2D()(x)
    x = tf.keras.layers.Dense(32, activation="relu")(x)
    out = tf.keras.layers.Dense(N_CLASSES, activation="softmax")(x)
    model = tf.keras.Model(inp, out)
    model.compile(
        optimizer=tf.keras.optimizers.Adam(1e-3),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
    )
    return model


def emit_c_array(name: str, arr: np.ndarray) -> str:
    flat = np.ascontiguousarray(arr).astype(np.float32).ravel()
    parts = ", ".join(f"{v:.8e}f" for v in flat)
    return f"static const float {name}[{flat.size}u] = {{ {parts} }};\n"


def emit_headers(fb: np.ndarray, hann: np.ndarray, model, out_dir: str) -> None:
    import tensorflow as tf

    w, b = [], []
    for layer in model.layers:
        if isinstance(layer, (tf.keras.layers.Conv2D, tf.keras.layers.Dense)):
            w.append(layer.get_weights()[0].astype(np.float32))
            b.append(layer.get_weights()[1].astype(np.float32))

    cfg = os.path.join(out_dir, "edgi_mel_config.h")
    with open(cfg, "w", encoding="utf-8") as f:
        f.write("/* Auto-generated by train_edgi_open_cnn.py */\n")
        f.write("#ifndef EDGI_MEL_CONFIG_H\n#define EDGI_MEL_CONFIG_H\n\n")
        f.write(f"#define EDGI_MEL_SAMPLE_RATE_HZ  {SAMPLE_RATE}u\n")
        f.write(f"#define EDGI_MEL_WIN_SAMPLES     {WIN_SAMPLES}u\n")
        f.write(f"#define EDGI_MEL_HOP_SAMPLES     {HOP_SAMPLES}u\n")
        f.write(f"#define EDGI_MEL_CLIP_SAMPLES    {CLIP_SAMPLES}u\n")
        f.write(f"#define EDGI_MEL_N_FFT           {N_FFT}u\n")
        f.write(f"#define EDGI_MEL_N_BANDS         {N_MELS}u\n")
        f.write(f"#define EDGI_MEL_N_FRAMES        {N_FRAMES}u\n\n")
        f.write("#endif /* EDGI_MEL_CONFIG_H */\n")

    fb_path = os.path.join(out_dir, "edgi_mel_filterbank.h")
    with open(fb_path, "w", encoding="utf-8") as f:
        f.write("/* Auto-generated by train_edgi_open_cnn.py */\n")
        f.write("#ifndef EDGI_MEL_FILTERBANK_H\n#define EDGI_MEL_FILTERBANK_H\n\n")
        f.write("#include \"edgi_mel_config.h\"\n\n")
        f.write(emit_c_array("edgi_mel_hann", hann))
        f.write(emit_c_array("edgi_mel_filterbank", fb))
        f.write("\n#endif /* EDGI_MEL_FILTERBANK_H */\n")

    # layers: conv0, conv1, dense0, dense1
    c0w, c0b = w[0], b[0]
    c1w, c1b = w[1], b[1]
    d0w, d0b = w[2], b[2]
    d1w, d1b = w[3], b[3]

    cnn = os.path.join(out_dir, "edgi_open_cnn_weights.h")
    with open(cnn, "w", encoding="utf-8") as f:
        f.write("/* Auto-generated: micro_speech-style CNN (Apache-2.0 架构参考) */\n")
        f.write("#ifndef EDGI_OPEN_CNN_WEIGHTS_H\n#define EDGI_OPEN_CNN_WEIGHTS_H\n\n")
        f.write(f"#define EDGI_OPEN_CNN_CLASSES {N_CLASSES}u\n")
        f.write(f"#define EDGI_OPEN_CNN_CONV0_FILTERS {c0w.shape[3]}u\n")
        f.write(f"#define EDGI_OPEN_CNN_CONV0_KH {c0w.shape[0]}u\n")
        f.write(f"#define EDGI_OPEN_CNN_CONV0_KW {c0w.shape[1]}u\n")
        f.write(f"#define EDGI_OPEN_CNN_CONV1_FILTERS {c1w.shape[3]}u\n")
        f.write(f"#define EDGI_OPEN_CNN_CONV1_KH {c1w.shape[0]}u\n")
        f.write(f"#define EDGI_OPEN_CNN_CONV1_KW {c1w.shape[1]}u\n")
        f.write(f"#define EDGI_OPEN_CNN_DENSE0 {d0w.shape[1]}u\n\n")
        f.write(emit_c_array("edgi_open_cnn_conv0_w", c0w))
        f.write(emit_c_array("edgi_open_cnn_conv0_b", c0b))
        f.write(emit_c_array("edgi_open_cnn_conv1_w", c1w))
        f.write(emit_c_array("edgi_open_cnn_conv1_b", c1b))
        f.write(emit_c_array("edgi_open_cnn_dense0_w", d0w))
        f.write(emit_c_array("edgi_open_cnn_dense0_b", d0b))
        f.write(emit_c_array("edgi_open_cnn_dense1_w", d1w))
        f.write(emit_c_array("edgi_open_cnn_dense1_b", d1b))
        f.write("\n#endif /* EDGI_OPEN_CNN_WEIGHTS_H */\n")

    print("wrote", cfg)
    print("wrote", fb_path)
    print("wrote", cnn)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav-root", default="demo_wav")
    ap.add_argument("--epochs", type=int, default=24)
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    np.random.seed(args.seed)
    fb, hann = build_mel_filterbank()
    X, Y = load_dataset(os.path.expanduser(args.wav_root), fb, hann)
    print("dataset", X.shape, "labels", [int((Y == i).sum()) for i in range(N_CLASSES)])

    import tensorflow as tf

    tf.random.set_seed(args.seed)
    model = build_keras_model()
    idx = np.arange(X.shape[0])
    np.random.shuffle(idx)
    split = int(X.shape[0] * 0.85)
    tr, te = idx[:split], idx[split:]
    Xtr = X[tr][..., np.newaxis]
    Xte = X[te][..., np.newaxis]
    ytr, yte = Y[tr], Y[te]

    model.fit(
        Xtr,
        ytr,
        validation_data=(Xte, yte),
        epochs=args.epochs,
        batch_size=32,
        verbose=2,
    )
    print("test acc", model.evaluate(Xte, yte, verbose=0)[1])

    include = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "include"))
    emit_headers(fb, hann, model, include)


if __name__ == "__main__":
    main()
