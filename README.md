# Edgi-Talk

基于 PSoC Edge E84 Edgi-Talk 双核开发板与 RT-Thread 的听障人士环境声音识别与多模态提醒系统。

系统在设备端识别环境、火警、敲门、婴儿啼哭和水开五类声音。M33 负责采音、log-mel 特征和轻量卷积网络推理；M55 负责 LVGL 中文界面、WiFi、HTTP 告警上传；云端服务部署在使用者自己的 ECS，并通过钉钉机器人发送远程通知。

## 主要特点

- 16 kHz 单声道音频，1 s 窗口，49×40 log-mel 特征。
- 五分类轻量 CNN：2,085 个参数，float32 权重约 8.14 KiB。
- M33/M55 通过固定 64 B 共享结构通信，采用 32 B 分区、内存屏障、cache 维护和 seq 双读。
- LVGL 中文四宫格告警界面、最近记录及 WiFi/Cloud 状态。
- 自建 ECS 接收 HTTP POST，并异步转发钉钉 Webhook。

## 目录

- `M33_Edgi-Talk_Audio`：采音、特征、模型推理、门控和共享内存发布。
- `M55_Edgi_Talk_LVGL`：共享内存读取、LVGL、WiFi、HTTP 上传和云端服务。
- `模型与验证`：当前模型及离线验证结果。
- `现场工具`：电脑端 WiFi 配网工具。
- `资源与测试证据`：模型、双核通信和固件资源口径。

## 使用自己的 ECS

本仓库不提供公共服务器，也不绑定参赛团队使用过的云主机。每位使用者需要：

1. 在自己的 ECS 上部署 `M55_Edgi_Talk_LVGL/deploy/ecs/edgi_alarm_server.py`。
2. 复制 `edgi_alarm.env.example` 为 `/opt/edgi_alarm.env`。
3. 设置自己的 `EDGI_API_KEY` 和 `DINGTALK_WEBHOOK`。
4. 在 ECS 防火墙和安全组中放行所选 HTTP 端口。
5. 在板端将 `ALARM_HTTP_HOST` 改为自己的 ECS IP 或域名，并让 `ALARM_API_KEY` 与云端一致。

详细步骤见 [ECS 自建部署说明](docs/ECS_DEPLOYMENT.md)。

## 构建说明

仓库只保留作品自行实现和重点修改的代码，不包含完整 RT-Thread SDK、厂商软件包和编译缓存。请在 RT-Thread Studio 中使用对应 Edgi-Talk 板级支持包导入两套工程。

## 测试口径

- 离线验证：657/848，总体准确率 77.5%。
- 环境门控：707/730 保持背景，96.8%。
- 现场四类测试：56/80，总体正确率 70.0%。

动态内存最高水位、端到端时延和连续运行时长没有完整实测记录，因此不作为性能结论。

## 安全提示

- 不要把真实 WiFi 密码、钉钉 Webhook、API Key 或私钥提交到 GitHub。
- `CHANGE_ME`、`YOUR_TOKEN` 和 `YOUR_ECS_IP_OR_DOMAIN` 均为占位符。
- 建议使用足够长的随机 API Key，并在公网部署时增加 HTTPS、访问控制和日志审计。

## License

本项目中参赛团队原创代码采用 MIT License。第三方组件仍遵循其各自许可证。
