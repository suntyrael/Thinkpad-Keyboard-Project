# Thinkpad Wireless 电源管理模式 · 判断依据与开关机流程

> 依据（2026-08-19 现状，ZMK pin `6e2ef41e`）：
> - 板级自定义：`module/boards/thinkpad/thinkpad_wireless/{board.c, status_leds.c, board_hw.h}`
> - 用户配置：`config/thinkpad_wireless.conf`（`CONFIG_ZMK_SLEEP` / `ZMK_IDLE_SLEEP_TIMEOUT` / `CONFIG_POWEROFF` / `CONFIG_ZMK_EXT_POWER`）
> - ZMK 框架：`zmk/app/src/activity.c`、`zmk/app/src/pm.c`、`zmk/app/src/ext_power_generic.c`、`kscan_*` 驱动
> - 硬件信号：PWRSWITCH `P1.11`（低有效）、HOTKEY `P1.08`（低有效）、5V_EN `P0.12`（高有效）、BOOST 芯片 ETA1061（EN 低 = True Shutdown）

---

## 一、模式总览

| # | 模式 | 进入条件（判断依据） | 5V Boost | MCU 状态 | 可唤醒源 / 退出方式 |
|---|------|---------------------|----------|----------|---------------------|
| 1 | **运行 Active** | 上电 / 任意唤醒 | 开 | 全速运行 | — |
| 2 | **空闲 Idle** | 无操作 ≥ 30 s（`ZMK_IDLE_TIMEOUT` 默认 30000 ms） | 开（TrackPoint 继续供电） | 运行 | 任意按键 / TrackPoint 移动（重新计入 ACTIVE） |
| 3 | **深度睡眠 Deep Sleep** | 无操作 ≥ 15 min（`ZMK_IDLE_SLEEP_TIMEOUT=900000`）**且无 USB 供电** | 关（PM SUSPEND → 5V_EN 拉低） | **System OFF**（`sys_poweroff()`） | 任意按键（矩阵键 / HOTKEY / PWRSWITCH，经 GPIOTE DETECT）；USB 插入（nRF52 USB DETECT 硬件唤醒） |
| 4 | **手动关机 Soft-Off** | 长按 PWRSWITCH **8 s**（status_leds 线程轮询） | 关（关机前显式拉低） | System OFF（`GPREGRET=0xAA`） | 任意按键会唤醒 → 启动时**需 PWRSWITCH 长按 2 s 确认**，否则立即再次关机 |
| 5 | **低电量关机** | SoC < **2 %** 且 **无 VBUS**（每 10 s 电池上报判定） | 关（防过放） | System OFF（`GPREGRET=0xAA`） | 同上：下次开机需 PWRSWITCH 长按 2 s（若 USB 在场则确认后保持开机） |
| 6 | **USB 充电态 / 蓝牙模式** | VBUS 在场（`zmk_usb_is_powered()`），无主机枚举时 USB 仅供电 | 开 | 运行，**永不进入 Deep Sleep**（activity.c 显式排除） | **蓝牙保持正常工作**（广播/连接/输出与插电无关，见下） |

**关键规则：USB 在场 = 永不深度睡眠。** `activity_work_handler` 中 `inactive_time > MAX_SLEEP_MS && !is_usb_power_present()` 才进 SLEEP——插着 USB 充电/使用时只有 Idle，不会掉电关机。

**适配器（无主机枚举）充电 = 蓝牙模式，不是 USB 模式。** ZMK 区分"供电"与"HID 就绪"两个判定：插适配器时 `zmk_usb_is_powered()=true`（VBUS 检测，`ZMK_USB_CONN_POWERED`）但 `zmk_usb_is_hid_ready()=false`（未枚举）——前者保证充电期不睡眠/不低电关机，后者使 `endpoints.c` 输出路由 fallback 到 BLE（`Falling back to BLE`）；`ble.c` 广播逻辑不读任何 USB 状态，广播/连接/按键输出照常。唯一例外：System OFF（0xAA）状态下插适配器，USB DETECT 唤醒后因电源键未按被 0xAA 门立即回睡，需长按电源键 2 s 开机后才进入蓝牙模式（充电不受影响）。

---

## 二、各模式判断依据详解

### 1) 运行 → 空闲（Idle）
- 判断：`zmk/app/src/activity.c`，每秒定时器计算 `inactive_time`，超过 `CONFIG_ZMK_IDLE_TIMEOUT`（本工程未覆盖，用 ZMK 默认 **30 s**）→ `ZMK_ACTIVITY_IDLE`。
- 计数重置：按键（`zmk_position_state_changed`）、传感器、**TrackPoint 输入**（`INPUT_CALLBACK_DEFINE`，`CONFIG_ZMK_POINTING` 下启用）都会 `note_activity()`。
- 表现：`status_leds.c` 收到 `zmk_activity_state_changed` → `is_idle=true` → **BT LED 关闭**、电池 LED 关闭（省电），电源呼吸灯保持。

### 2) 空闲 → 深度睡眠（Deep Sleep / System OFF）
- 判断：`inactive_time > CONFIG_ZMK_IDLE_SLEEP_TIMEOUT`（本工程显式 **900000 ms = 15 min**）且 `!is_usb_power_present()`。
- 流程（activity.c L77-88）：
  1. `set_state(ZMK_ACTIVITY_SLEEP)` → 广播事件；
  2. `zmk_pm_suspend_devices()`：对所有 PM 设备做 SUSPEND——
     - **ext-power**（`ext_power_generic.c` PM action）：`5V_EN(P0.12) 拉低` → ETA1061 True Shutdown，TrackPoint 断电；
     - PWM0 切 `pwm0_sleep` pinctrl（`low-power-enable`）；
     - **kscan（wakeup-source）跳过挂起**，保持 GPIO SENSE 使能以捕获唤醒；
  3. `sys_poweroff()` → nRF52840 **System OFF**（需 `CONFIG_POWEROFF=y`，已启用）。
- 唤醒：任意按键 → GPIOTE DETECT（System OFF 硬件唤醒）→ 冷启动复位。**GPREGRET 未被置位 → 正常开机，无 2 s 确认门槛**，5V 由 ext-power 初始化自动恢复，TrackPoint 走 POR 时序。

### 3) 手动关机（Soft-Off，PWRSWITCH 长按 8 s）
- 判断：`status_leds.c` LED 线程（80 ms 周期）轮询 `P1.11` 原始电平，连续低电平 **100 tick = 8 s**。
- 流程（status_leds.c led_thread_fn）：
  1. `NRF_POWER->GPREGRET = 0xAA`（`MANUAL_POWER_OFF_FLAG`）——防误触标记；
  2. 灯效：5 个状态灯全亮 300 ms → 依序熄灭（BT→绿→红→MicMute→Mute，各 150 ms）→ 电源灯灭；
  3. `5V_EN 拉低`（关 TrackPoint 供电）；
  4. `sys_poweroff()`。
- 与 keymap 的关系：PWRSWITCH（pos 129）绑定 **`&none`**，**不向主机发送 HID 电源键**（review 2.16 已修复）；短按无任何行为。

### 4) 手动/低电关机后的开机（2 s 确认门）
- 判断：`board.c` PRE_KERNEL_2 阶段检查 `GPREGRET == 0xAA`。
- 流程（board_gpio_init）：
  1. 配置 PWRSWITCH 为输入上拉；
  2. 读电平：**未按住**（包里误触唤醒）→ 拉低 5V_EN → `sys_poweroff()`，立即回睡；
  3. 按住 → **busy-wait 轮询 2 s**（20 × 100 ms），期间松开 → 同样直接回睡；
  4. 确认满 2 s → `GPREGRET = 0`（清除标记）→ 开机灯序（BT→绿→红→MicMute→Mute 依序亮，各 150 ms，全灭）→ 正常启动；
  5. 之后 ZMK 层正常初始化：5V_EN 恢复高（ext-power 初始化默认 enable）→ TrackPoint POR → 蓝牙/USB 就绪。
- **低电关机复用同一标记**（review 2.18 修复）：低电关机前也写 `0xAA`，防止包里误触陷入"唤醒→低电→再关机"循环耗尽余电。

### 5) 低电量关机
- 判断：`status_leds.c` LED 线程，`battery_soc < 2 && !vbus_present`（每 10 s `CONFIG_ZMK_BATTERY_REPORT_INTERVAL=10` 更新 SoC，滞后 ≤ 10 s）。
- 流程：红灯快闪 5 次（100 ms 周期）→ 写 `GPREGRET=0xAA` → 拉低 5V_EN → 关灯 → `sys_poweroff()`。

---

## 三、开关机方式汇总

| 操作 | 方式 | 时长 | 结果 |
|------|------|------|------|
| 开机（常规睡眠后） | 任意按键 / TrackPoint | 即时 | 正常启动，无确认门 |
| 开机（手动/低电关机后） | **长按 PWRSWITCH** | **2 s**（<2 s 松手 = 继续关机） | 清除 0xAA 标记，灯序动画后正常启动 |
| 开机（低电关机后充电） | 插入 USB（USB DETECT 唤醒）+ 长按 PWRSWITCH 2 s | 2 s | VBUS 在场 → 保持开机充电 |
| 开机（彻底断电后） | 电池接入即上电 | — | 正常启动 |
| 自动睡眠 | 无操作（含 TrackPoint）且无 USB | 15 min | System OFF，5V 关 |
| 手动关机 | **长按 PWRSWITCH** | **8 s** | 写 0xAA 标记 → 灯序 → 5V 关 → System OFF |
| 低电关机 | SoC < 2% 且无 USB | 自动 | 红灯闪 5 次 → 同手动关机（写 0xAA） |
| 配对广播 | Fn/ThinkVantage + 长按 PWRSWITCH | ≥ 2 s（無顺序依赖） | **板级触发**：status_leds 层 1 激活+电源键 2 s 窗口 → `zmk_ble_prof_select(0)`，电源灯快闪 |
| 蓝牙切换 | Fn/ThinkVantage + 1..5 | 短按 | `&bt BT_SEL 0..4` |

---

## 四、防误触与安全机制

1. **GPREGRET 标记（0xAA）**：手动关机 / 低电关机写入；下次任何唤醒都先走 2 s 电源键确认，杜绝包里误触耗电。
2. **5V_EN 双保险**：所有关机路径（手动 8 s、低电、0xAA 假唤醒回睡）都显式拉低 `P0.12`，ETA1061 True Shutdown 隔离 TrackPoint 漏电。
3. **低电关机写标记**：防止"唤醒→低电→关机"循环（review 2.18）。
4. **USB 在场不睡眠**：充电中不会被 15 min 定时关进 System OFF；低电关机判定同样排除 USB 供电场景。
5. **配对不再依赖按键顺序（2026-08-19 修复）**：旧实现要求“先按层键（mo 1）再按电源键”——ZMK 在按下瞬间解析绑定（层 0 按下 = `&none`，无法挽回），且状态_leds 在按下瞬间快_layer 状态。适配器充电（USB POWERED）时键盘无 LED 反馈、易按错序 → 实测无法配对。现改为**板级配对**：status_leds 检测“按住电源键期间层 1 曾激活”即开 2 s 窗口，满足后直接调 `zmk_ble_prof_select(0)`（LED 快闪与触发合一）；keymap 的 `ht_bt_pair` 已移除。消除顺序死锁。

## 七、已修复的开关机竞态（2026-08-19）

**现象：适配器插入（无主机枚举）时无法配对；8 s 关机后按 2 s 无法开机。**

**根因（均经 nRF52840 PS 原文 + 驱动源码确认）：**
1. **关机竞态**：PS GPIO 章节明文“Setting the system to System OFF while DETECT is high will cause a wakeup”。8 s 关机流程中用户通常仍按着电源键 → `sys_poweroff()` 瞬间 PWRSWITCH(SENSE=LOW) 触发 DETECT=高 → 芯片刚进 System OFF 就被自身 GPIO 唤醒 → 旧 0xAA 门“未按/早松 → 立即回睡”与之竞争，开机窗口仅约 100ms → 实测“按 2 s 无法开机”。修复：关机灯效后**等待电源键松开**（≤1 s）再进 System OFF。
2. **0xAA 门过窄**：唤醒后未按电源键就立即回睡。修复：改为 **8 s 等待窗口**（任意唤醒源都给用户 8 s 开机的机会，超时才回睡，防误触保留）+ 唤醒时打印 RESETREAS 便于实测定位。
3. **配对顺序死锁**：见上文“不再依赖按键顺序”。

**待实测确认的硬件边界：**若电源键**严格按顺序**（先 Fn/ThinkVantage、后电源 2 s）在适配器插入时仍无法配对，则代码层无断点，需硬件排查充电器输出纹波/供电噪声导致 MCU 复位或按键事件丢失（换适配器复测或示波器量 VDD）。

---

## 五、相关代码定位

| 逻辑 | 位置 |
|------|------|
| Idle/Sleep 状态机、睡眠条件、sys_poweroff | `zmk/app/src/activity.c`（L74-94） |
| 睡眠前设备挂起（含 ext-power 关闭） | `zmk/app/src/pm.c`、`zmk/app/src/ext_power_generic.c`（PM action） |
| 手动 8 s 关机、低电关机、灯效 | `module/boards/thinkpad/thinkpad_wireless/status_leds.c`（led_thread_fn） |
| 0xAA 唤醒确认门、开机灯序、假唤醒回睡 | `module/boards/thinkpad/thinkpad_wireless/board.c`（board_gpio_init） |
| 引脚/标记定义（单一来源） | `module/boards/thinkpad/thinkpad_wireless/board_hw.h` |
| 电源键 keymap 绑定（`&none` / ht_bt_pair） | `config/thinkpad_wireless.keymap`（row 8，pos 129） |
| wakeup-source、ext-power 节点、PWM sleep pinctrl | `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless.dts` |
| 睡眠/关机相关 Kconfig | `config/thinkpad_wireless.conf`、`thinkpad_wireless_defconfig` |

## 六、已知边界与待验证

- **USB 插入能否从 System OFF 唤醒**：nRF52840 硬件支持 USB DETECT 唤醒，Zephyr `udc_nrf` 已启用 VBUS 检测（`caps.can_detect_vbus`），但本项目未在板级显式验证过该路径（review 3.3）。
- **System OFF → PWRSWITCH 唤醒路径**（0xAA 门）为实测建议项（review 3.3），代码逻辑已闭环。
- **浅睡眠（WFI）不存在**：本板睡眠 = System OFF（`CONFIG_ZMK_SLEEP` 下 `sys_poweroff()`），无 System ON idle 中间态。
- `CONFIG_ZMK_PM_SOFT_OFF` 未启用：ZMK 标准 soft-off（`&soft_off` 行为）未接线，电源键开关机完全由板级裸 GPIO 实现（review 2.13 提出过收敛建议，未实施）。
