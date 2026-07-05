# ECS 自建部署说明

## 1. 准备自己的云服务器

准备一台可运行 Python 3 的 Linux ECS，并确认能够通过公网 IP 或域名访问。不要直接复用他人的服务器地址、API Key 或钉钉 Webhook。

## 2. 上传服务程序

将 `M55_Edgi_Talk_LVGL/deploy/ecs` 上传到 ECS，例如：

```bash
sudo mkdir -p /opt/edgi_alarm
sudo cp edgi_alarm_server.py /opt/edgi_alarm/
sudo cp edgi-alarm.service /etc/systemd/system/
```

## 3. 配置密钥与钉钉

创建 `/opt/edgi_alarm.env`：

```ini
EDGI_API_KEY=请替换为足够长的随机字符串
DINGTALK_WEBHOOK=https://oapi.dingtalk.com/robot/send?access_token=请替换为自己的令牌
EDGI_LISTEN_HOST=0.0.0.0
EDGI_LISTEN_PORT=80
```

请勿把这个实际配置文件提交到 GitHub。

## 4. 启动服务

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now edgi-alarm.service
sudo systemctl status edgi-alarm.service
```

在 ECS 安全组中放行所用端口。正式公网使用建议通过 Nginx 或其他反向代理提供 HTTPS。

## 5. 本机验证

```bash
curl -sS http://127.0.0.1/health

curl -X POST http://127.0.0.1/api/alarm/report \
  -H "Content-Type: application/json" \
  -H "X-Api-Key: 你的API_KEY" \
  -d '{"device_id":"test","alarm_code":"fire","alarm_msg":"火灾告警","level":2}'
```

## 6. 配置开发板

在 `M55_Edgi_Talk_LVGL/applications/alarm_http.c` 中配置：

```c
#define ALARM_HTTP_HOST "你的ECS公网IP或域名"
#define ALARM_HTTP_PORT 80
#define ALARM_API_KEY   "与云端一致的API_KEY"
```

也可以在工程构建配置中定义这些宏，避免直接写入源码。重新编译并烧录 M55 后，通过 `alarm_post` 命令先验证 HTTP，再开启自动上传。
