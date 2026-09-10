# 语音对话整合验证记录

日期：2026-09-10

## 已通过

- ECS 服务自动测试：40 项通过。
- 板端通信、AI 指令限制、语音状态、共享录音缓冲和界面字体检查：11 组通过。
- M33 从干净状态编译成功：`text=160400, data=14032, bss=244565`。
- M55 从干净状态编译成功：`text=1087780, data=4860, bss=4548844`。
- 先编译 M33、再增量编译 M55 已实测通过；双核不再共用不兼容的临时文件。
- 语音共享区为 `0x261C1000..0x261D0FFF`，位于链接文件预留的 `0x261C0000..0x261FFFFF` 内，不与 M55 堆或显示缓冲重叠。
- Git 追踪文件中未发现百炼 API Key 或板端私密配置；真实配置文件已忽略。
- 电脑已识别 Edgi-Talk 的 KitProg3 CMSIS-DAP、KitProg3 bridge 和 COM6。

## 当前候选固件

- M33：`D:\edgi_pet\M33_Edgi_Pet\build\rtthread.hex`
  - SHA-256：`8688E5BC1F3C302BFC10A5363154901FB49C57587E2D7DA1AAD117B57EAD1089`
- M55：`D:\edgi_pet\M55_Edgi_Pet_A1\rtthread.hex`
  - SHA-256：`1511EA7782B8E389B4FBEFBA6174BE6F61BD020C77AB5BF9AD36F111A0477E31`

M55 候选固件编译时板端私密配置仍为 `UNSET`，因此不得将这一版作为最终语音固件烧录。

## 待完成的真机验收

- ECS 公网 `http://47.116.168.34/healthz` 当前返回 404，语音网关尚未完成公网部署。
- 在本机运行 `tools\configure_board_voice.ps1`，填入 Wi-Fi 和与 ECS 相同的设备令牌。
- 重新编译并更新固件校验值。
- 备份当前板内固件，按 M33、M55 顺序烧录。
- 断开电脑数据线后，验收普通中文对话、查看状态、记忆挑战、断网重连和超时恢复。

## 回退

当前已验证可启动的 AB1 固件仍保留在 `D:\edgi_pet\output`，更早的双核固件保留在 `D:\edgi_pet\output\previous`。如新语音版启动失败，仍按 M33、M55 顺序烧回这一对固件。
