#!/bin/bash
# One-time install on Ubuntu ECS (run as root in Workbench).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET_PY="/opt/edgi_alarm_server.py"
TARGET_UNIT="/etc/systemd/system/edgi-alarm.service"

if [[ ! -f "$SCRIPT_DIR/edgi_alarm_server.py" ]]; then
  echo "Run from deploy/ecs directory (edgi_alarm_server.py missing)."
  exit 1
fi

# Stop manual nohup instances
pkill -f 'python3.*edgi_alarm_server.py' 2>/dev/null || true
sleep 1

install -m 755 "$SCRIPT_DIR/edgi_alarm_server.py" "$TARGET_PY"
install -m 644 "$SCRIPT_DIR/edgi-alarm.service" "$TARGET_UNIT"

if [[ ! -f /opt/edgi_alarm.env ]]; then
  echo "Create /opt/edgi_alarm.env with DINGTALK_WEBHOOK (see edgi_alarm.env.example)."
  install -m 600 "$SCRIPT_DIR/edgi_alarm.env.example" /opt/edgi_alarm.env.example
fi

systemctl daemon-reload
systemctl enable edgi-alarm.service
systemctl restart edgi-alarm.service
systemctl status edgi-alarm.service --no-pager

cat <<'EOF'

Health:
  curl -sS --max-time 2 http://127.0.0.1/health

Alarm test:
  curl -sS --max-time 5 -X POST http://127.0.0.1/api/alarm/report \
    -H "Content-Type: application/json" \
    -H "X-Api-Key: YOUR_API_KEY" \
    -d '{"device_id":"test","alarm_code":"fire","alarm_msg":"火灾告警","level":2}'
EOF
