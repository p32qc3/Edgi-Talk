# ECS 语音服务部署

## 私密配置

真实配置只保存在 ECS 的 `/etc/edgi-voice/edgi-voice.env`。内容为：

```dotenv
DASHSCOPE_API_KEY=百炼控制台创建的北京区域API_Key
DASHSCOPE_WORKSPACE_ID=北京Base_URL中从ws-开始到.cn-beijing之前的部分
EDGI_DEVICE_ID=edgi-talk-01
EDGI_DEVICE_TOKEN=openssl_rand_-hex_32生成的结果
EDGI_LISTEN_HOST=127.0.0.1
EDGI_LISTEN_PORT=8000
```

不要在聊天、截图、GitHub 或板子固件中公开 API Key。设备密码需要同时填入板子的私有配置文件。

## 安装

将仓库放到 ECS 后，在仓库根目录执行：

```bash
chmod +x deploy/install_voice_gateway.sh
./deploy/install_voice_gateway.sh
```

安装程序会检查私密配置、安装 Python 环境、启动语音服务与 nginx，并执行本机健康检查。

## 检查

```bash
systemctl status edgi-voice --no-pager
curl http://127.0.0.1:8000/healthz
curl http://47.116.168.34/healthz
```

三个检查应显示服务正在运行或返回 `{"status":"ok"}`。如果公网检查失败，在阿里云 ECS 安全组中只新增 TCP 端口 80 的入方向规则；SSH 端口保持原有设置。

## 更新

更新仓库后重新运行安装程序。真实配置文件不会被覆盖。

## 说明

当前课程演示版使用 HTTP，录音传输未加密。API Key 始终只存在 ECS。后续绑定域名和证书时，将 nginx 入口升级为 HTTPS 即可，板子业务接口不变。
