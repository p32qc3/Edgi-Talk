# Communication and AI Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 生成可编译、可验证、可烧录的 AB1 固件，并把团队自编源码同步到 `p32qc3/Edgi-Talk`。

**Architecture:** M33 保留音频与识别职责并增加 Flash 协调；新 M55 A1 工程承载宠物界面、通信和 AI。纯 C 协议与 AI 逻辑使用电脑端测试，板端桥接通过完整固件编译和真机启动验证。

**Tech Stack:** RT-Thread、C99、LVGL、UART5、SCons、GNU Arm Embedded 13.3、OpenOCD

**Spec:** `docs/superpowers/specs/2026-09-09-member-a-comm-ai-integration-design.md`

## Global Constraints

- 保留原 `M55_Edgi_Pet`，新工程名固定为 `M55_Edgi_Pet_A1`。
- 串口固定为 UART5、115200 波特率。
- 状态版本固定为 `AB1`。
- 烧录顺序固定为 M33→M55。
- 不提交 SDK、密钥、编译缓存和旧固件。

---

### Task 1: 可靠通信完成事件

**Files:**
- Modify: `M55_Edgi_Pet_A1/applications/comm_ai/comm_link.h`
- Modify: `M55_Edgi_Pet_A1/applications/comm_ai/comm_link.c`
- Test: `tests/test_comm_link.c`

**Interfaces:**
- Consumes: `comm_link_start_game`、`comm_link_cancel_game`、协议 ACK 帧。
- Produces: `COMM_EVENT_TX_ACKED`，事件携带命令、会话和游戏编号。

- [ ] 编写测试：GAME_START 的正确 ACK 只产生一次成功事件，错误 ACK 不产生成功事件。
- [ ] 运行测试并确认因缺少 `COMM_EVENT_TX_ACKED` 而失败。
- [ ] 在发送时保存游戏编号，在接受 ACK 后产生成功事件。
- [ ] 运行通信测试，确认握手、重发、拒绝、重复接收和成功确认全部通过。
- [ ] 提交可靠通信改动。

### Task 2: AI 动作执行

**Files:**
- Create: `M55_Edgi_Pet_A1/applications/comm_ai/ai_action.h`
- Create: `M55_Edgi_Pet_A1/applications/comm_ai/ai_action.c`
- Test: `tests/test_ai_action.c`

**Interfaces:**
- Consumes: `AiEvent` 和 `AI_ACTION_TEXT/CELEBRATE/START_GAME`。
- Produces: `ai_action_decide(const AiEvent *, AiActionDecision *)`，只把完成的 `simon` 动作转换为开始游戏请求。

- [ ] 编写测试：完成的 Simon 动作请求游戏；超时、取消、拒绝和未知动作都不执行游戏。
- [ ] 运行测试并确认因接口尚不存在而失败。
- [ ] 实现无系统依赖的动作判定函数。
- [ ] 运行 AI 解析、超时和动作测试并确认通过。
- [ ] 提交 AI 动作改动。

### Task 3: A 与 B 的板端桥接及构建修复

**Files:**
- Modify: `M55_Edgi_Pet_A1/applications/comm_ai/comm_ai_bridge.c`
- Modify: `M55_Edgi_Pet_A1/applications/comm_ai/SConscript`
- Modify: `M55_Edgi_Pet_A1/applications/pet_logic/SConscript`
- Modify: `M55_Edgi_Pet_A1/.config`
- Modify: `M55_Edgi_Pet_A1/rtconfig.h`
- Modify: `M33_Edgi_Pet/applications/main.c`
- Modify: `M33_Edgi_Pet/applications/SConscript`

**Interfaces:**
- Consumes: Task 1 的成功确认事件、Task 2 的动作判定、A 的公开 `pet_app_*` 接口。
- Produces: 51 上线/离线、游戏开始/取消/结果回传，以及可触发 Simon 的离线 AI 演示。

- [ ] 记录当前 M55 链接失败，确认缺少协议实现和 Flash 驱动。
- [ ] 将 `messages.c`、`reliable.c` 和所需 SMIF 驱动加入构建，并启用 UART5。
- [ ] 用成功 ACK 调用 `pet_app_report_game_started/cancelled`，游戏结果继续调用 `pet_app_report_game_result`。
- [ ] 让离线 AI 演示通过严格 JSON 解析产生 Simon 动作并调用 `pet_app_request_game`。
- [ ] 将 M33 Flash 协调轮询放入独立高优先级线程，避免占用主循环职责。
- [ ] 全量编译 M33 和 M55，检查关键符号和地址范围。
- [ ] 提交板端整合改动。

### Task 4: 仓库同步、烧录与真机验证

**Files:**
- Create: `M55_Edgi_Pet_A1/applications/**`（仅团队源码）
- Create: `pet_shared/pet_flash_gate.h`
- Modify: `README.md`
- Create: `docs/AB1_BUILD_AND_FLASH.md`

**Interfaces:**
- Consumes: Task 3 生成的 M33、M55 HEX。
- Produces: GitHub 可审核源码和板上 AB1 版本。

- [ ] 将经过编译验证的团队源码和测试同步到独立 Git 分支，不复制 SDK 或缓存。
- [ ] 运行全部电脑端测试和源码安全检查。
- [ ] 备份当前 HEX，并确认板卡与目标芯片。
- [ ] 依次烧录 M33、M55并执行校验。
- [ ] 读取串口和共享内存，确认双核启动、AB1 状态和心跳变化。
- [ ] 提交文档与最终源码，并推送 `feat/comm-ai-core`。
