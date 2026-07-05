# 开源轻量音频模型（Open CNN）

## 方案说明

已接入 **TensorFlow Lite Micro Speech** 同款思路（Apache-2.0，[tflite-micro/micro_speech](https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/micro/examples/micro_speech)）：

| 环节 | 实现 |
|------|------|
| 前端 | 16 kHz → 1 s 缓冲 → log-mel **49×40** |
| 模型 | 2 层 Conv2D + GAP + Dense，共 **2,085 个参数**，float32 权重约 **8.14 KiB** |
| 输出 | 5 类 **真实 softmax**（环境/火/敲/婴/水） |
| 板上 | 纯 C 推理，**不依赖** TFLM 运行时包 |

默认开关：`EDGI_AI_USE_OPEN_CNN=1`（见 `edgi_audio_capture.h`）。

网络结构为：`49×40×1 → Conv2D(8, 5×5, same) → MaxPool(4×4) → Conv2D(16, 3×3, same) → GAP → Dense(32) → Dense(5) + softmax`。当前板端采用静态数组和纯 C 前向推理，识别过程中不动态申请内存。

上电串口应打印：

```text
[AI] mic=52 infer=open_cnn (micro_speech-style log-mel+CNN)
```

## 训练 / 更新权重

```bash
cd Edgi-Talk_Audio/applications/m33/tools
pip install numpy tensorflow scipy scikit-learn
python prepare_sound_source_for_edgi.py          # 从 声音源 生成 demo_wav
python train_edgi_open_cnn.py --wav-root demo_wav --epochs 24
```

生成文件（自动写入 `../include/`）：

- `edgi_mel_config.h` / `edgi_mel_filterbank.h`
- `edgi_open_cnn_weights.h`

然后 **全量编译烧录 M33**（新增源文件：`edgi_mel_frontend.c`、`app_ai_open_cnn.c`）。

## 用你自己的演示音重训（推荐）

手机外放与 PC 训练分布不一致时，用 **MIC 旁录** 的 wav 替换 `demo_wav/0..4/`，再跑上面训练命令。

目录结构：

```text
demo_wav/
  0/  quiet.wav
  1/  fire.wav
  2/  knock.wav
  3/  baby.wav
  4/  boil.wav
```

## 与其它后端切换

在 `edgi_audio_capture.h` 中：

| 宏 | 含义 |
|----|------|
| `EDGI_AI_USE_OPEN_CNN 1` | 开源 CNN（默认） |
| `EDGI_AI_USE_OPEN_CNN 0` + `EDGI_AI_USE_DEMO_CENTROID 0` | 旧 40 维 MLP |
| `EDGI_AI_USE_DEMO_CENTROID 1` | 模板/demo_cal |

## 参考开源项目

- [tensorflow/tflite-micro micro_speech](https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/micro/examples/micro_speech) — 架构与 mel 参数参考
- [STMicroelectronics/yamnet](https://huggingface.co/STMicroelectronics/yamnet) — 更大模型，需 TFLM + 更多 Flash（未默认启用）

若需 **完整 TFLite Micro + .tflite 文件** 部署，可在 RT-Thread 菜单打开 `PKG_USING_TENSORFLOWLITEMICRO` 后，用 `train_edgi_open_cnn.py` 扩展导出 `.tflite`；当前工程采用 **权重直嵌 + 纯 C** 以降低集成成本。
