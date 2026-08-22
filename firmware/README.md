# Firmware Assets

本目录存放烧录固件的辅助资产（bootloader）与完整镜像生成说明。

## 版本固件命名约定（2026-08-21 起）

- `thinkpad_wireless_BMD340_full_v{版本}.hex` — **bmd340-module 分支**完整镜像
  （bootloader + SoftDevice + 应用 + bootloader settings 有效标记 + UICR，全片烧录用）
- `thinkpad_wireless_BMD340_app_v{版本}.uf2` — 同分支应用镜像（UF2 格式，U 盘拖拽用）
- nRF52840_CoB 分支沿用旧式 `thinkpad_wireless_full_v{版本}.hex` 命名。
- 版本号取开发日志下一版本（如现为 v0.2）。

### v0.2（2026-08-21）— BMD-340 模组首个正式发布版本（纯 HID 模式，关闭调试串口）

- **目标硬件**：专用于 **u-blox BMD-340 模组**（`bmd340-module` 分支），板载天线，LFCLK 内部 RC 32.768kHz。
- **发布特性**：关闭 USB CDC 虚拟调试串口与 PS/2 中断日志，保持纯净 USB HID / BLE HID 键盘模式，极低待机功耗与内存占用（FLASH 30.70% / RAM 26.74%）。
- **修复：PrtSc 键 Windows 无响应**。transform 中 pos 105（PSCRN）的矩阵坐标在
  2026-08-17 去重修复（68a019c）中被误改为 X220 矩阵的空位（0x00），导致物理
  PrtSc（实际位于 sense1/drive13）落到 pos 93 的 `&trans` 上，固件未发送任何 HID。
  已恢复为 X220 官方坐标（见 `thinkpad_wireless.dts` Row 6 注释），幽灵坐标移入
  pos 93（unused）。
- **修复：Fn 键完全无事件**。`status_leds.c` 开机初始化对 P1.08（HOTKEY/Fn）与
  P1.11（PWRSWITCH）裸调用 `gpio_pin_configure()`，触发 Zephyr gpio_nrfx
  “Remove previously configured trigger when pin is reconfigured” 行为，把 direct
  kscan 已挂好的 GPIOTE 中断删除。已移除这两行配置，恢复 Fn 与电源键扫描事件。
- **ThinkVantage 蓝牙切换层（Layer 1）**：
  - `ThinkVantage + F1~F5`：切换蓝牙设备 1~5（`BT_SEL 0~4`）
  - `ThinkVantage + 电源键（长按 ≥2s）`：进入配对广播模式（4 灯快闪）
- **Fn 扩展多媒体与控制层（Layer 2）**：
  - `Fn + F2`：工作站锁定（`Win + L`）
  - `Fn + F3`：电池/电源菜单（`Win + X`）
  - `Fn + F4`：系统睡眠（`Consumer Sleep`）
  - `Fn + F5`：WiFi / 蓝牙快捷控制面板（`Win + A`）
  - `Fn + F6`：摄像头开关（`Ctrl + Shift + O`）
  - `Fn + F7`：屏幕输出投影切换（`Win + P`）
  - `Fn + F8`：小红点 TrackPoint 电源硬件开关（`&ext_power EP_TOG` 切换 5V 升压）
  - `Fn + F12`：系统休眠（`Consumer Sleep Mode`）
  - `Fn + Space`：网页放大（`Ctrl + =`）
  - `Fn + Home`：屏幕亮度调高（`C_BRI_UP`）
  - `Fn + End`：屏幕亮度调低（`C_BRI_DN`）
  - `Fn + PrtSc`：SysRq 功能（`&kp SYSREQ`）
  - `Fn + ScrLk`：NumLock 数字锁定功能（`&kp KP_NUM`）
  - `Fn + Pause`：Break 中断功能（`Ctrl + Pause` / `&kp LC(PAUSE_BREAK)`）
  - `Fn + 上箭头`：停止媒体播放（`&kp C_STOP`）
  - `Fn + 下箭头`：媒体播放/暂停（`&kp C_PP`）
  - `Fn + 左箭头`：下一首（`&kp C_NEXT`）
  - `Fn + 右箭头`：上一首（`&kp C_PREV`）
- **固件指标**：应用 SP=0x2000F658 / RESET=0x2EB35 / size=0x3CC90，FLASH 30.70% / RAM 26.74%。
- 验证状态（2026-08-21）：键盘全键矩阵、ThinkVantage 蓝牙切换/配对、Fn 全套组合键、USB HID 正常；除 PS/2（小红帽）与电池功能外其余功能调试 OK。

### v1.0.47（2026-08-21）

- **修复：USB 调试串口缺失**。上一版本地构建未应用 build.yaml 的 snippets，
  固件只有 HID/MOUSE 没有 CDC ACM。本轮以 `west -S "studio-rpc-usb-uart zmk-usb-logging"`
  重建（与 CI 等价），`zephyr,console`/Studio RPC 两个 CDC 接口均已启用。
- 应用向量：SP=0x200261E8 / RESET=0x35141；bootloader settings bank_0=0x0001、size=0x4F878；
  FLASH 40.17% / RAM 63.80%。
- 烧录后应出现：HID 键盘 + MOUSE（小红帽）+ USB 调试串口（zmk-usb-logging）。

### v1.0.46（2026-08-21）

- 同步 nRF52840_CoB 全部改进（PS/2 v5-v12、电源 v20-v27、ThinkVantage BT 层等 30 项）
- `CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y`：LFCLK 改用内部 RC，不依赖外置 32.768kHz 晶振 X1
  （当前硬件未焊 X1；焊上后可删除该配置恢复 XTAL）
- 应用向量：SP=0x200226D8 / RESET=0x2EAD9；bootloader settings bank_0=0x0001、size=0x3CA8C

## Bootloader

`adafruit_bootloader_zmk.hex` — Adafruit nRF52 Bootloader（**ZMK 布局变体**，
应用起始地址 0x26000，s140 版）。

- 来源：https://nicekeyboards.com/assets/nice_nano_bootloader-0.6.0_s140_6.1.1.hex
  （nice!nano 官方 bootloader，ZMK 生态标准）
- 用途：合并生成完整烧录镜像（bootloader + 应用）；也可单独用 J-Link 恢复 bootloader。
- 覆盖区域：0x00000-0x00B00（引导向量表头）、0xF4000-0x100000（bootloader 主体）、
  0x10001014-0x1000101C（UICR）。
- **注意**：不要使用 nrfmicro 的 nosd 变体（`nrfmicro_nrf52840_bootloader-*-nosd.hex`）——
  其无 SoftDevice 时应用起始地址为 0x1000，与 ZMK 的 0x26000 布局不兼容，会导致
  bootloader 判定应用无效并停留在 DFU 模式（插入 USB 弹出升级盘）。

## 完整镜像生成

GitHub Actions 构建时通过 `tools/merge_hex.py` 将上述 bootloader 与应用固件
（`thinkpad_wireless-zmk.hex`，起始 0x26000）合并为 `thinkpad_full_0x0000.hex`
（覆盖 0x0 起完整布局，全片烧录用，可勾选 "Erase all"）。

本地手动合并：

```sh
python3 tools/merge_hex.py output_full.hex \
    firmware/adafruit_bootloader_zmk.hex \
    <应用hex>
```

## 烧录注意事项（重要）

1. 完整镜像（`thinkpad_full_0x0000.hex`）烧录时必须勾选 **"Erase all"**。
2. 完整镜像已通过 `tools/merge_hex.py --mark-app-valid` 在 bootloader settings 页（0xFF000）
   写入 `bank_0 = BANK_VALID_APP (0x0001)` 标记。**这是 J-Link 直接烧录后能正常启动的关键**——
   bootloader 依赖该标记判定应用有效；缺失时（settings 页保持擦除态）会判定应用无效并
   停留在 UF2 模式（弹 U盘）。UF2 拖拽流程会自动写该标记，J-Link 直烧不会。
3. 若手动烧录旧版完整镜像（无标记），可执行：
   `nrfutil device write --address 0xFF000 --value 0x00000001`
