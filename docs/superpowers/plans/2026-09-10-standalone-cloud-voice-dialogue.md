# Edgi-Talk Standalone Cloud Voice Dialogue Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a low-load, push-to-talk Chinese voice loop that runs on Edgi-Talk without a PC, relays through the user's ECS, speaks Qwen's reply, and safely triggers allow-listed pet actions.

**Architecture:** M33 remains the sole audio hardware owner and exchanges bounded PCM blocks with M55 through the reserved M33/M55 shared RAM. M55 owns the microphone UI, Wi-Fi, HTTP turn state, and pet actions. A small FastAPI service on ECS keeps the Bailian secret, converts one HTTP turn into a Qwen-Audio push-to-talk WebSocket session, resamples the 24 kHz reply to 16 kHz, and serves the result back to the board.

**Tech Stack:** RT-Thread/C on Cortex-M33 and Cortex-M55, LVGL, lwIP sockets, Python 3.10, FastAPI, Uvicorn, websockets, pytest, nginx, systemd, Alibaba Cloud Bailian Qwen-Audio Realtime.

**Spec:** `docs/superpowers/specs/2026-09-10-standalone-cloud-voice-dialogue-design.md`

## Global Constraints

- Model ID is exactly `qwen-audio-3.0-realtime-flash`, using a Beijing-region API Key and workspace.
- Input is PCM 16 kHz, signed 16-bit, mono; ECS converts Qwen's PCM 24 kHz reply to PCM 16 kHz before board playback.
- One recording is at most 8 seconds (`256000` bytes); silence stop is about 1.5 seconds; one reply is at most 12 seconds.
- Device-to-ECS transport is HTTP for the classroom version; API Key never leaves ECS and real secrets never enter Git.
- M33 owns `mic0` and `sound0`; M55 must not initialize the codec or audio I2C.
- AI/network failure must not block LVGL, pet logic, storage, IMU, or UART5 communication.
- Actions are limited to `none`, `show_status`, `start_simon`, and `home`; unknown actions become `none`.
- New firmware is always built and flashed in the order M33 then M55; current AB1 images remain the rollback pair.

## Configuration Locations

The user fills only these two private files after they are created by the tasks below:

1. ECS: `/etc/edgi-voice/edgi-voice.env`

   ```dotenv
   DASHSCOPE_API_KEY=UNSET
   DASHSCOPE_WORKSPACE_ID=UNSET
   EDGI_DEVICE_ID=edgi-talk-01
   EDGI_DEVICE_TOKEN=UNSET
   EDGI_LISTEN_HOST=127.0.0.1
   EDGI_LISTEN_PORT=8000
   ```

2. Board-side private file: `M55_Edgi_Pet_A1/applications/voice/voice_private_config.h`

   ```c
   #define EDGI_VOICE_SERVER_HOST "47.116.168.34"
   #define EDGI_VOICE_SERVER_PORT 80
   #define EDGI_VOICE_DEVICE_ID   "edgi-talk-01"
   #define EDGI_VOICE_DEVICE_TOKEN "UNSET"
   ```

Before deployment, run `openssl rand -hex 32` once on ECS and place that exact result in both token fields. Replace both ECS `UNSET` values with the Bailian API Key and the `ws-...` identifier shown in the Beijing OpenAI-compatible Base URL. The board file contains only ECS connection data, never `DASHSCOPE_API_KEY`. Wi-Fi SSID/password continue to use the board's existing persistent Wi-Fi configuration.

---

### Task 1: ECS configuration and allow-listed intent core

**Files:**
- Create: `ecs/voice_gateway/__init__.py`
- Create: `ecs/voice_gateway/config.py`
- Create: `ecs/voice_gateway/intent.py`
- Create: `ecs/tests/test_config.py`
- Create: `ecs/tests/test_intent.py`
- Create: `ecs/requirements.txt`

**Interfaces:**
- Consumes: process environment variables listed in Configuration Locations.
- Produces: `Settings.from_env() -> Settings` and `classify_intent(transcript: str) -> str`.

- [ ] **Step 1: Write failing configuration and intent tests**

  ```python
  def test_settings_rejects_missing_secret(monkeypatch):
      monkeypatch.delenv("DASHSCOPE_API_KEY", raising=False)
      with pytest.raises(ValueError, match="DASHSCOPE_API_KEY"):
          Settings.from_env()

  @pytest.mark.parametrize(("text", "action"), [
      ("我们聊聊天吧", "none"),
      ("让我看看小团子的状态", "show_status"),
      ("开始记忆灯游戏", "start_simon"),
      ("回到主页", "home"),
      ("控制任意引脚", "none"),
  ])
  def test_intent_allow_list(text, action):
      assert classify_intent(text) == action
  ```

- [ ] **Step 2: Run the tests and confirm the new modules are missing**

  Run: `python -m pytest ecs/tests/test_config.py ecs/tests/test_intent.py -q`

  Expected: collection fails because `voice_gateway.config` and `voice_gateway.intent` do not exist.

- [ ] **Step 3: Implement strict environment loading and deterministic intent matching**

  `Settings` must reject an empty API key, workspace ID, device ID, or token shorter than 32 characters. `classify_intent` must normalize whitespace and match only explicit status, Simon, and home phrases, returning one of the four exact action strings.

  ```python
  @dataclass(frozen=True)
  class Settings:
      api_key: str
      workspace_id: str
      device_id: str
      device_token: str
      listen_host: str = "127.0.0.1"
      listen_port: int = 8000

      @classmethod
      def from_env(cls) -> "Settings": ...

  ALLOWED_ACTIONS = frozenset({"none", "show_status", "start_simon", "home"})
  ```

- [ ] **Step 4: Run the focused tests**

  Run: `python -m pytest ecs/tests/test_config.py ecs/tests/test_intent.py -q`

  Expected: all tests pass.

- [ ] **Step 5: Commit the ECS core**

  ```text
  git add ecs/voice_gateway ecs/tests ecs/requirements.txt
  git commit -m "feat: add voice gateway configuration and intents"
  ```

### Task 2: Qwen push-to-talk adapter

**Files:**
- Create: `ecs/voice_gateway/qwen_realtime.py`
- Create: `ecs/tests/test_qwen_realtime.py`
- Modify: `ecs/requirements.txt`

**Interfaces:**
- Consumes: `Settings.api_key`, `Settings.workspace_id`, and complete 16 kHz PCM bytes.
- Produces: `await QwenRealtime.complete_turn(pcm: bytes) -> QwenTurn(transcript: str, reply_text: str, pcm16: bytes)`.

- [ ] **Step 1: Write failing event-aggregation tests with a fake WebSocket**

  Cover `conversation.item.input_audio_transcription.completed`, `response.audio_transcript.done`, multiple `response.audio.delta` chunks, `response.done`, malformed Base64, provider `error`, and timeout. Assert input is sent as 3200-byte chunks, followed by `input_audio_buffer.commit` and `response.create`.

  ```python
  result = await client.complete_turn(b"\x01\x00" * 16000)
  assert result.transcript == "查看状态"
  assert result.reply_text == "小团子现在很开心。"
  assert 30000 <= len(result.pcm16) <= 34000
  ```

- [ ] **Step 2: Run the Qwen tests and confirm failure**

  Run: `python -m pytest ecs/tests/test_qwen_realtime.py -q`

  Expected: import failure for `QwenRealtime`.

- [ ] **Step 3: Implement the official WebSocket event sequence**

  Build the URL as:

  ```python
  url = (
      f"wss://{settings.workspace_id}.cn-beijing.maas.aliyuncs.com/"
      "api-ws/v1/realtime?model=qwen-audio-3.0-realtime-flash"
  )
  ```

  Send `session.update` with `modalities=["audio", "text"]`, `voice="longanqian"`, Chinese concise pet-companion instructions, and `turn_detection=None`. Append Base64 PCM in 3200-byte pieces, send `input_audio_buffer.commit`, then `response.create`. Collect transcript/text/audio until `response.done`, enforce a 20-second provider timeout, cap the output at 24 kHz × 2 bytes × 12 seconds, and convert 24 kHz mono PCM to 16 kHz mono PCM using `audioop.ratecv` on Python 3.10.

- [ ] **Step 4: Run the adapter tests**

  Run: `python -m pytest ecs/tests/test_qwen_realtime.py -q`

  Expected: all tests pass without contacting Alibaba Cloud.

- [ ] **Step 5: Commit the Qwen adapter**

  ```text
  git add ecs/voice_gateway/qwen_realtime.py ecs/tests/test_qwen_realtime.py ecs/requirements.txt
  git commit -m "feat: connect voice turns to Qwen realtime"
  ```

### Task 3: Authenticated ECS HTTP turn service

**Files:**
- Create: `ecs/voice_gateway/audio_store.py`
- Create: `ecs/voice_gateway/app.py`
- Create: `ecs/tests/test_app.py`

**Interfaces:**
- Consumes: raw PCM POST bodies and `QwenRealtime.complete_turn`.
- Produces: `POST /v1/voice/turn`, `GET /v1/voice/audio/{audio_id}`, and `GET /healthz`.

- [ ] **Step 1: Write failing HTTP contract tests**

  Tests must cover missing/wrong authentication, body sizes `0`, `639`, `640`, `256000`, and `256001`, invalid sample rate, cached duplicate `turn_id`, action classification, audio ownership, expiry, provider timeout, and health response. The success schema is exact:

  ```json
  {
    "turn_id": "00000001-00000001",
    "transcript": "查看状态",
    "reply_text": "小团子现在很开心。",
    "action": "show_status",
    "audio_id": "32-lowercase-hex-characters",
    "expires_in": 60
  }
  ```

- [ ] **Step 2: Run the endpoint tests and confirm failure**

  Run: `python -m pytest ecs/tests/test_app.py -q`

  Expected: imports fail for the HTTP app and store.

- [ ] **Step 3: Implement bounded temporary storage, auth, idempotency, and error codes**

  Use `hmac.compare_digest` for device token checks. Keep at most eight completed turns and eight audio replies in memory, expire them after 60 seconds, and never write recordings to disk. Return only `AUTH`, `BAD_AUDIO`, `TOO_LARGE`, `BUSY`, `UPSTREAM`, or `TIMEOUT` to the board. Duplicate `(device_id, turn_id)` requests return the cached JSON without calling Qwen again.

- [ ] **Step 4: Run all ECS unit tests**

  Run: `python -m pytest ecs/tests -q`

  Expected: all tests pass.

- [ ] **Step 5: Commit the HTTP service**

  ```text
  git add ecs/voice_gateway/audio_store.py ecs/voice_gateway/app.py ecs/tests/test_app.py
  git commit -m "feat: expose authenticated voice turn service"
  ```

### Task 4: ECS deployment files and live provider smoke test

**Files:**
- Create: `deploy/edgi-voice.env.example`
- Create: `deploy/edgi-voice.service`
- Create: `deploy/nginx-edgi-voice.conf`
- Create: `deploy/install_voice_gateway.sh`
- Create: `ecs/tests/manual_qwen_smoke.py`
- Create: `ecs/tests/test_deploy_files.py`
- Create: `docs/voice-ecs-deployment.md`

**Interfaces:**
- Consumes: Task 3 ASGI app and the private `/etc/edgi-voice/edgi-voice.env`.
- Produces: nginx port 80 → Uvicorn `127.0.0.1:8000`, automatic restart, and a repeatable live smoke test.

- [ ] **Step 1: Add a deployment-file validation test**

  The test must assert that the service reads `EnvironmentFile=/etc/edgi-voice/edgi-voice.env`, runs as `edgi-voice`, binds only to localhost, sets `Restart=on-failure`, nginx caps body size at 300 KiB, and no real API key/token occurs in tracked files.

- [ ] **Step 2: Run the validation and confirm failure**

  Run: `python -m pytest ecs/tests/test_deploy_files.py -q`

  Expected: FAIL because deployment files do not exist.

- [ ] **Step 3: Add the installer, service, nginx proxy, example environment, and deployment guide**

  `install_voice_gateway.sh` must create `/opt/edgi-voice`, a Python virtual environment, an unprivileged service account, `/etc/edgi-voice` mode `750`, and a private environment file mode `600`; then install and enable nginx and systemd units. It must stop if any required environment value is still `UNSET`.

- [ ] **Step 4: Run local tests and one ECS live smoke test**

  Local: `python -m pytest ecs/tests -q`

  ECS after the user enters the private values:

  ```text
  curl http://127.0.0.1:8000/healthz
  python /opt/edgi-voice/ecs/tests/manual_qwen_smoke.py /opt/edgi-voice/testdata/nihao.pcm
  curl http://47.116.168.34/healthz
  ```

  Expected: health returns `{"status":"ok"}` and the smoke script receives non-empty transcript, reply text, and 16 kHz PCM.

- [ ] **Step 5: Commit deployment support**

  ```text
  git add deploy ecs/tests/manual_qwen_smoke.py ecs/tests/test_deploy_files.py docs/voice-ecs-deployment.md
  git commit -m "ops: add ECS voice gateway deployment"
  ```

### Task 5: Shared PCM protocol and M33 audio ownership

**Files:**
- Create: `pet_shared/edgi_voice_shm.h`
- Create: `pet_shared/edgi_voice_ring.c`
- Create: `tests/test_voice_ring.c`
- Modify: `tests/run_host_tests.ps1`
- Modify: `M33_Edgi-Talk_Audio/applications/m33/SConscript`
- Modify: `M33_Edgi-Talk_Audio/applications/m33/include/edgi_audio_capture.h`
- Modify: `M33_Edgi-Talk_Audio/applications/m33/src/drv_pdm.c`
- Create: `M33_Edgi-Talk_Audio/applications/m33/include/edgi_voice_m33.h`
- Create: `M33_Edgi-Talk_Audio/applications/m33/src/edgi_voice_m33.c`
- Modify: `M33_Edgi-Talk_Audio/applications/m33/src/app_ai.c`

**Interfaces:**
- Consumes: shared RAM `0x261C1000..0x261D0FFF`, M55 record/play commands, existing `mic0` and `sound0`.
- Produces: `edgi_voice_ring_write/read`, `edgi_voice_m33_is_recording`, `edgi_voice_m33_publish_pcm`, and 16 kHz playback.

- [ ] **Step 1: Write failing host tests for the shared ring**

  Cover wraparound, ordered blocks, a full ring refusing writes, empty reads, reset generation, invalid magic/version, and duplicate command sequence. Use a byte-array fake shared region and assert unread data is never overwritten.

- [ ] **Step 2: Run host tests and confirm the voice test fails**

  Run: `./tests/run_host_tests.ps1`

  Expected: existing five tests pass and `test_voice_ring` fails to compile because the new protocol is absent.

- [ ] **Step 3: Implement the 64 KiB shared region and M33 bridge**

  Reserve address `0x261C1000`, magic `0x564F4943` (`VOIC`), version `1`, and total size `0x10000`. Use two 32-byte-aligned bounded rings inside the region and compile-time assertions that the structure fits. M33 polls record/play command sequences; `app_ai.c` passes each already-captured 20 ms frame to `edgi_voice_m33_publish_pcm` and skips local classification while a cloud recording is active. Playback pauses capture, configures `sound0` for 16 kHz/16-bit/mono, streams downlink data, then resumes capture.

- [ ] **Step 4: Run host tests and build M33**

  Run: `./tests/run_host_tests.ps1`

  Build in the full workspace: `scons -C D:/edgi_pet/M33_Edgi_Pet -j8`

  Expected: all host tests pass; M33 links without shared-region overflow; the map contains `edgi_voice_m33_*`.

- [ ] **Step 5: Commit M33 audio exchange**

  ```text
  git add pet_shared/edgi_voice_shm.h pet_shared/edgi_voice_ring.c tests M33_Edgi-Talk_Audio/applications/m33
  git commit -m "feat: bridge M33 audio through shared memory"
  ```

### Task 6: Pure M55 voice state machine and response parser

**Files:**
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_session.h`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_session.c`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_response.h`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_response.c`
- Create: `M55_Edgi_Pet_A1/applications/voice/SConscript`
- Create: `tests/test_voice_session.c`
- Create: `tests/test_voice_response.c`
- Modify: `tests/run_host_tests.ps1`

**Interfaces:**
- Consumes: timestamped tap/audio/network/playback events.
- Produces: `voice_session_dispatch(VoiceSession *, const VoiceEvent *) -> VoiceEffect` and strict `VoiceResponse` parsing.

- [ ] **Step 1: Write failing state and JSON tests**

  Test `IDLE → LISTENING → THINKING → SPEAKING → IDLE`, second-tap stop, 1.5-second silence stop after speech begins, 8-second hard stop, 25-second cloud timeout, cancel, Wi-Fi loss, duplicate `turn_id`, 12-second audio cap, and all unknown actions mapping to `none`.

- [ ] **Step 2: Run host tests and confirm failure**

  Run: `./tests/run_host_tests.ps1`

  Expected: the two new test executables fail to compile because the voice modules are missing.

- [ ] **Step 3: Implement deterministic state transitions and a bounded JSON schema**

  Keep the core free of RT-Thread/LVGL calls. The largest transcript is 192 bytes, reply text 256 bytes, action 16 bytes, audio ID 33 bytes, and whole JSON 768 bytes. `VoiceEffect` may request exactly one side effect at a time: start record, stop record, upload, start playback, cancel, or execute action.

- [ ] **Step 4: Run all host tests**

  Run: `./tests/run_host_tests.ps1`

  Expected: all old and new tests pass.

- [ ] **Step 5: Commit the M55 core**

  ```text
  git add M55_Edgi_Pet_A1/applications/voice tests
  git commit -m "feat: add bounded M55 voice session core"
  ```

### Task 7: M55 private config, Wi-Fi, shared audio, and HTTP client

**Files:**
- Modify: `.gitignore`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_private_config.h.example`
- Create locally, never commit: `M55_Edgi_Pet_A1/applications/voice/voice_private_config.h`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_config.h`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_shm_client.h`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_shm_client.c`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_http.h`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_http.c`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_service.h`
- Create: `M55_Edgi_Pet_A1/applications/voice/voice_service.c`
- Modify: `M55_Edgi_Pet_A1/rtconfig.h`
- Modify: full-build configuration under `D:/edgi_pet/M55_Edgi_Pet_A1`

**Interfaces:**
- Consumes: Task 5 shared protocol, Task 6 state/effects, persistent RT-Thread Wi-Fi, and ECS endpoints.
- Produces: `voice_service_toggle()`, `voice_service_cancel()`, `voice_service_get_view(VoiceView *)`, and background record/upload/download/play orchestration.

- [ ] **Step 1: Add compile-time privacy and configuration tests**

  Verify `.gitignore` excludes `voice_private_config.h`, the example contains only the non-secret `UNSET` sentinel, `voice_config.h` refuses `UNSET` in release builds, and `git grep` finds no `DASHSCOPE_API_KEY=` assignment outside the example environment file.

- [ ] **Step 2: Run host/privacy tests and confirm failure**

  Run: `./tests/run_host_tests.ps1`

  Expected: configuration test fails because files and ignore rule are absent.

- [ ] **Step 3: Implement the background service and fixed-size HTTP client**

  Allocate one 256000-byte recording buffer at service initialization and fail gracefully if unavailable. Read M33 frames into that buffer, compute energy for silence detection, POST raw PCM with `Content-Length`, `X-Device-ID`, `X-Device-Token`, `X-Turn-ID`, and `X-Sample-Rate: 16000`, then GET the authenticated audio path and stream it into the downlink ring. Use a 5-second connect timeout, 25-second whole-turn timeout, one retry with the same turn ID, a 768-byte JSON buffer, and no LVGL calls from the worker thread.

- [ ] **Step 4: Port only the proven Wi-Fi/network settings from `M55_Edgi_Talk_LVGL` and build M55**

  Enable RT-Thread Wi-Fi host driver, netdev, lwIP, SAL/POSIX socket support, automatic reconnect, and persisted credentials. Preserve A1 LVGL, IMU, storage, UART5, and board settings. Keep codec/I2C initialization disabled on M55.

  Build: `scons -C D:/edgi_pet/M55_Edgi_Pet_A1 -j8`

  Expected: M55 links, `voice_service` is present, Wi-Fi/network packages initialize, and no `voice_private_config.h` content appears in Git status.

- [ ] **Step 5: Commit M55 transport support**

  ```text
  git add .gitignore M55_Edgi_Pet_A1/applications/voice M55_Edgi_Pet_A1/rtconfig.h tests
  git commit -m "feat: connect M55 voice service to ECS"
  ```

### Task 8: Microphone UI and safe pet action bridge

**Files:**
- Modify: `M55_Edgi_Pet_A1/applications/pet_pages.c`
- Modify: `M55_Edgi_Pet_A1/applications/pet_pages.h`
- Modify: `M55_Edgi_Pet_A1/applications/pet_app.c`
- Modify: `M55_Edgi_Pet_A1/applications/pet_app.h`
- Modify: `M55_Edgi_Pet_A1/applications/comm_ai/ai_action.h`
- Modify: `M55_Edgi_Pet_A1/applications/comm_ai/ai_action.c`
- Modify: `M55_Edgi_Pet_A1/applications/comm_ai/pet_service_status.c`
- Create: `tests/test_voice_action_bridge.c`

**Interfaces:**
- Consumes: `VoiceView` and one validated action per unique turn.
- Produces: clickable top voice-status pill, user-visible voice states, `pet_app_apply_voice_action(turn_id, action)`, and existing `pet_app_navigate`/`pet_app_request_game` calls.

- [ ] **Step 1: Write failing action bridge tests**

  Assert `show_status` navigates to `PET_PAGE_STATUS`, `home` navigates to `PET_PAGE_HOME`, `start_simon` calls `pet_app_request_game(PET_GAME_SIMON)`, the same turn ID is ignored twice, and unknown actions do nothing. Also assert Simon reports 51 offline instead of success when the existing game link is unavailable.

- [ ] **Step 2: Run host tests and confirm failure**

  Run: `./tests/run_host_tests.ps1`

  Expected: action bridge symbols are missing.

- [ ] **Step 3: Replace the top status pill with a clickable microphone/status control**

  Reuse the current top status area instead of adding a new page. Draw the microphone with LVGL shapes so no unsupported emoji glyph is required. The caption maps exactly to `点击说话`, `正在听…`, `思考中…`, `正在回答…`, `网络未连接`, `网络异常，请重试`, and `没有听清，请重试`. The click callback only queues `voice_service_toggle`; LVGL stays owner-thread-only.

- [ ] **Step 4: Implement one-time action dispatch and run tests/build**

  Route only the four allow-listed actions. Keep the existing offline AI shell command as a fallback diagnostic, but change the service indicator from `AI:DEMO` to voice-state text when cloud voice is configured.

  Run: `./tests/run_host_tests.ps1`

  Build: `scons -C D:/edgi_pet/M55_Edgi_Pet_A1 -j8`

  Expected: all host tests and the M55 build pass.

- [ ] **Step 5: Commit UI and action integration**

  ```text
  git add M55_Edgi_Pet_A1/applications tests
  git commit -m "feat: add microphone dialogue interaction"
  ```

### Task 9: End-to-end verification, packaging, flash, and documentation

**Files:**
- Modify: `README.md`
- Create: `docs/voice-dialogue-test-record.md`
- Update locally: `D:/edgi_pet/output/Edgi_Pet_M33.hex`
- Update locally: `D:/edgi_pet/output/Edgi_Pet_M55.hex`
- Update locally: `D:/edgi_pet/output/build_manifest.txt`

**Interfaces:**
- Consumes: deployed ECS, configured Wi-Fi, M33/M55 builds, Edgi-Talk board, optional 51 board.
- Produces: verified rollback-safe firmware pair and a reproducible test record.

- [ ] **Step 1: Run every automated check from clean inputs**

  ```text
  python -m pytest ecs/tests -q
  ./tests/run_host_tests.ps1
  scons -C D:/edgi_pet/M33_Edgi_Pet -c
  scons -C D:/edgi_pet/M33_Edgi_Pet -j8
  scons -C D:/edgi_pet/M55_Edgi_Pet_A1 -c
  scons -C D:/edgi_pet/M55_Edgi_Pet_A1 -j8
  ```

  Expected: all tests pass and both clean builds finish with exit code zero.

- [ ] **Step 2: Audit secrets and firmware memory maps**

  Confirm no API key/device token is tracked, M33/M55 shared structures agree in size/version/address, voice shared RAM ends no later than `0x261D0FFF`, and no application section overlaps `0x261C0000..0x261FFFFF`.

- [ ] **Step 3: Deploy ECS and verify public health/voice endpoints**

  Install the committed deployment bundle, fill `/etc/edgi-voice/edgi-voice.env` interactively on ECS, restart the service, and verify `/healthz`, authenticated sample upload, audio download, duplicate turn behavior, and rejection of a bad token. Never print the API key or device token into captured logs.

- [ ] **Step 4: Package and flash the board in the required order**

  Back up the existing AB1 pair. Package the M33 image with its secure boot component, then flash and verify M33. Flash and verify M55 second. Capture serial startup only long enough to confirm audio, shared memory, Wi-Fi, LVGL, UART5, and voice worker initialization.

- [ ] **Step 5: Run physical acceptance checks**

  Power the board from a charger or power bank with no PC data connection. Verify Chinese general chat, `查看状态`, `开始记忆灯`, second-tap stop, 1.5-second silence stop, 8-second forced stop, Wi-Fi loss/recovery, ECS stop/recovery, and Qwen timeout. If 51 is absent, verify the game action reports offline without blocking; repeat the real Simon round-trip when 51 is available.

- [ ] **Step 6: Record hashes, outcomes, and rollback instructions**

  Put firmware SHA-256 values, build sizes, test results, ECS service status, board observations, known HTTP limitation, and rollback image names into `docs/voice-dialogue-test-record.md` and `D:/edgi_pet/output/build_manifest.txt`.

- [ ] **Step 7: Commit the verified handoff**

  ```text
  git add README.md docs/voice-dialogue-test-record.md
  git commit -m "docs: verify standalone voice dialogue"
  ```
