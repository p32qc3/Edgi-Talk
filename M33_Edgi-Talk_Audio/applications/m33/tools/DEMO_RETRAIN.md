# 演示用手机播放 — 烧录与重训步骤

## 已用 workspace/声音源 训练（当前工程）

| 文件 | 类别 |
|------|------|
| 火灾警报(1).m4a | 1 火警 |
| 敲门声(1).m4a | 2 敲门 |
| 婴儿哭泣(1).m4a | 3 婴儿 |
| 水沸腾(1).m4a | 4 水开 |

一键更新权重（PC 需 `pip install imageio-ffmpeg numpy scikit-learn`）：

```bash
cd Edgi-Talk_Audio/applications/m33/tools
python prepare_sound_source_for_edgi.py
python generate_edgi_mlp_weights.py --wav-root demo_wav --class-weight balanced
```

然后 **全量编译烧录 M33**（`include/edgi_mlp_weights.h` 已替换）。

离线验证（四类 wav 主类准确率）：火警/敲门/婴儿/水开 **均已通过主类投票**。

---

## 一、为什么要重烧 M33？

旧固件：`out = argmax(环境,火,敲,婴,水)` → 环境常 60%，**永远 type=0**。

当前推荐：**MLP + 差分特征 + softmax 置信度**（`EDGI_AI_USE_DEMO_CENTROID=0`）。
勿再用「模板 L2 + one-hot 100%」——稍响就饱和成全判火警。

**必须全量编译烧录 M33**（`edgi_mlp_weights.h` 与 `edgi_spec_features.c` 已更新）。

M55 建议同烧：`alarm_upload` 最低置信度 25%，与演示 conf≈24 对齐。

---

## 二、烧录后 5 分钟自测

1. M33 串口应出现：`[AI] DEMO mode: mic=72 pick=max(fire,knock,baby,boil) if>=12%`
2. 手机对准 **左下 MIC 孔**，15～20 cm，播火警 15 秒。
3. 看 `[AI] score ... => out=1`（火最高且 ≥12 即可，不要求超过 bg）。
4. M55：`wifi_start` → `wifi_join_edgi` → `alarm_upload 1` → 看屏变红格、`m33_peek type=1`。

若 **out 已是 1 但类不对**（敲门也显示火警）→ 做第三节重训。
若 **四类都 <12%** → 加大音量、靠近 MIC，或每类单独录 wav 重训。

---

## 三、让「四类手机声音」都认对（推荐，约 1 小时）

用 **演示时要播的同一条音频**，在 MIC 旁录进 PC（手机录 30s 即可）。

### 目录（16 kHz 单声道 wav 最省事）

```text
D:/demo_wav/
  0/  quiet_room.wav      # 环境：安静 30s
  1/  fire_phone.wav      # 火警：演示用火警 30s
  2/  knock_phone.wav     # 敲门
  3/  baby_phone.wav      # 婴儿
  4/  boil_phone.wav      # 烧水
```

可用 Audacity：轨道 → 重采样 16000 Hz → 导出 16-bit PCM mono。

### 训练并替换权重

```bash
cd Edgi-Talk_Audio/applications/m33/tools
python generate_edgi_mlp_weights.py --wav-root D:/demo_wav
```

把生成的 `../include/edgi_mlp_weights.h` 确认已更新，**只重编烧录 M33**。

终端会打印 `train acc` / `test acc`，建议 test acc > 0.7 再拍演示片。

### 演示拍摄

- **一类一声**，每段 15s，不要连续混播。
- `EDGI_AI_FIELD_TUNING` 可保持 1；重训后多数情况可改 0 用标准 argmax。

---

## 四、仍失败时

| 现象 | 处理 |
|------|------|
| 无 `[AI] DEMO mode` | 烧错工程或未全量编译 M33 |
| score 全 0 | mic0 未工作，查 `[AI] capture read fail` |
| out 有类但全变成 1 | 必须重训，且每类 wav 要分开 |
| 屏不变 | 烧 M55、查 seq 是否涨 |
| 无钉钉 | wifi=4、`alarm_upload 1`、conf≥25 |

保底镜头：`cm33 fire`（不依赖识别）。
