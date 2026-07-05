#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import sys
import threading
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

API_KEY = os.environ.get("EDGI_API_KEY", "")
HOST = os.environ.get("EDGI_LISTEN_HOST", "0.0.0.0")
PORT = int(os.environ.get("EDGI_LISTEN_PORT", "80"))
DINGTALK_TIMEOUT_S = float(os.environ.get("EDGI_DINGTALK_TIMEOUT", "5"))
DINGTALK_WEBHOOK = os.environ.get("DINGTALK_WEBHOOK", "")

ALARM_NAME = {
    "fire": "\u706b\u707e\u544a\u8b66",
    "knock": "\u6572\u95e8\u544a\u8b66",
    "baby": "\u5a74\u513f\u54ed\u58f0\u544a\u8b66",
    "boiling": "\u70e7\u6c34\u58f0\u544a\u8b66",
    "alarm_test": "\u6d4b\u8bd5\u544a\u8b66",
}


def load_env_file(path: str) -> None:
    global API_KEY, HOST, PORT, DINGTALK_TIMEOUT_S, DINGTALK_WEBHOOK
    if not os.path.isfile(path):
        return
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip().strip('"').strip("'")
            if key == "EDGI_API_KEY" and value:
                API_KEY = value
            elif key == "DINGTALK_WEBHOOK" and value:
                DINGTALK_WEBHOOK = value
            elif key == "EDGI_LISTEN_HOST" and value:
                HOST = value
            elif key == "EDGI_LISTEN_PORT" and value:
                PORT = int(value)
            elif key == "EDGI_DINGTALK_TIMEOUT" and value:
                DINGTALK_TIMEOUT_S = float(value)


load_env_file("/opt/edgi_alarm.env")


def notify_dingtalk(text: str) -> None:
    if not DINGTALK_WEBHOOK or "access_token=" not in DINGTALK_WEBHOOK:
        print("DING: skipped, DINGTALK_WEBHOOK not configured", flush=True)
        return

    body = json.dumps(
        {"msgtype": "text", "text": {"content": text[:2000]}},
        ensure_ascii=False,
    ).encode("utf-8")
    req = urllib.request.Request(
        DINGTALK_WEBHOOK,
        data=body,
        headers={"Content-Type": "application/json; charset=utf-8"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=DINGTALK_TIMEOUT_S) as resp:
            print("DING:", resp.read().decode("utf-8", errors="replace"), flush=True)
    except urllib.error.URLError as exc:
        print("DING error:", exc, flush=True)


def alarm_text(data: dict) -> str:
    code = str(data.get("alarm_code", "unknown"))
    msg = data.get("alarm_msg")
    if not isinstance(msg, str) or not msg.strip():
        msg = ALARM_NAME.get(code, code)
    return "\u544a\u8b66: %s | device=%s | level=%s" % (
        msg.strip(),
        data.get("device_id", "?"),
        data.get("level", "?"),
    )


class AlarmHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    timeout = 8

    def log_message(self, fmt, *args):
        print("[%s] %s" % (self.address_string(), fmt % args), flush=True)

    def send_json(self, status: int, data: dict) -> None:
        body = (json.dumps(data, ensure_ascii=False) + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/health", "/api/health"):
            self.send_json(200, {"ok": True, "service": "edgi-alarm"})
            return
        self.send_json(404, {"ok": False, "err": "not found"})

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path != "/api/alarm/report":
            self.send_json(404, {"ok": False, "err": "not found"})
            return
        if self.headers.get("X-Api-Key", "") != API_KEY:
            self.send_json(401, {"ok": False, "err": "bad api key"})
            return

        try:
            size = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            size = 0
        raw = self.rfile.read(size) if size > 0 else b""
        try:
            data = json.loads(raw.decode("utf-8"))
            if not isinstance(data, dict):
                raise ValueError("JSON body must be an object")
        except Exception as exc:
            print("BAD_JSON:", raw.decode("utf-8", errors="replace"), exc, flush=True)
            self.send_json(400, {"ok": False, "err": "bad json"})
            return

        print("ALARM_JSON:", data, flush=True)
        self.send_json(200, {"ok": True})
        threading.Thread(target=notify_dingtalk, args=(alarm_text(data),), daemon=True).start()

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError):
            pass


class AlarmServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main() -> int:
    if not API_KEY or API_KEY.startswith("CHANGE_ME"):
        print(
            "EDGI_API_KEY is not configured. Set it in /opt/edgi_alarm.env.",
            file=sys.stderr,
        )
        return 1
    if PORT < 1024 and hasattr(os, "geteuid") and os.geteuid() != 0:
        print("Port %d requires root. Run this service as root." % PORT, file=sys.stderr)
        return 1
    server = AlarmServer((HOST, PORT), AlarmHandler)
    print(
        "edgi-alarm listening on %s:%d, api_key=%s, dingtalk=%s"
        % (HOST, PORT, bool(API_KEY), bool(DINGTALK_WEBHOOK)),
        flush=True,
    )
    try:
        server.serve_forever()
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
