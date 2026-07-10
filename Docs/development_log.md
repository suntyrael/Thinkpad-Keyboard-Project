# ThinkPad 无线键盘开发与重构日志 (Development & Refactoring Log)

本文档用于记录 ThinkPad 键盘无线化改写项目（ZMK Firmware）的设计分析、重构逻辑及后续的每一次更新记录。

---

## 1. 硬件规格与引脚映射汇总

*   **主控芯片**：Nordic nRF52840-QIAA-R0 (Normal Voltage 模式，短接 VDD/VDDH 至 3.3V)
*   **低压差线性稳压器 (LDO)**：RT9080-33GJ5 (超低静态电流 Iq = 2µA，支持最大 600mA 输出)
*   **键盘排线连接器 (BTB)**：Molex 54363-0489 (40-Pin，完美兼容 X220 键盘排线)
*   **充电芯片**：TP4054 (设定电阻 2kΩ，充电电流 500mA)
*   **电量采样**：P0.02 (AIN0)，通过 100kΩ/100kΩ (50%) 分压电阻接至电池正极。
*   **5V 升压使能 (5V_EN)**：P0.12，高电平有效。用于控制板载 ETA1061 升压芯片，为指点杆（TrackPoint）提供 5V 供电，由 ZMK 的 External Power 模块自动控制。
*   **充电状态读取 (CHG_INT)**：P0.08，低电平代表正在充电，高电平/高阻态代表充电完成。

### 键盘矩阵引脚分配 (8x16 矩阵)
*   **行引脚 (Sense 0 ~ 7)**：`P0.26`, `P0.28`, `P0.05`, `P0.04`, `P0.27`, `P0.07`, `P1.12`, `P1.14`
*   **列引脚 (Drive 0 ~ 15)**：`P0.13`, `P0.20`, `P0.22`, `P0.24`, `P1.01`, `P0.25`, `P1.00`, `P0.21`, `P0.23`, `P0.16`, `P0.19`, `P0.15`, `P0.14`, `P1.05`, `P0.17`, `P1.03`

### 指针指点杆 (TrackPoint PS/2) 引脚
*   **时钟线 (TP4CLK)**：P1.13，带内部上拉。
*   **数据线 (TP4DATA)**：P1.10，带内部上拉。
*   **复位线 (TP4_RESET)**：P1.09，低电平有效。

### 独立直连按键
*   **ThinkVantage (HOTKEY)**：P1.08，带内部上拉，低电平有效。
*   **电源键 (PWRSWITCH)**：P1.11，带内部上拉，低电平有效。

### 指示灯接口 (低电平有效：0=亮，1=灭)
*   **Caps Lock 灯 (LEDCPSLOCK)**：P0.31
*   **电源灯 (LEDPWR)**：P0.29 (通过 PWM0 驱动)
*   **静音灯 (-LED_MUTE)**：P1.15
*   **麦克风静音灯 (-LEDMICMUTE_R)**：P1.07
*   **蓝牙状态灯 (BT_LED)**：P1.02
*   **电量红灯 (BAT_LED_R)**：P1.06
*   **电量绿灯 (BAT_LED_G)**：P1.04

---

## 2. 核心技术分析与设计

### 2.1 升级至 Zephyr 4.1.0 (HWMv2) 板级架构
新版 Zephyr 舍弃了传统的 `config/boards/arm/` 分类层级，要求将自定义板定义置于 `config/boards/<board_name>` 下。
*   **元数据定义**：新增 `board.yml` 声明 SoC `nrf52840` 和板子基本元数据。
*   **Kconfig 重写**：`Kconfig.board` 重命名为 `Kconfig.thinkpad_wireless`，并将旧的 `depends on` 依赖更改为 `select SOC_NRF52840_QIAA`。
*   **配置文件清理**：从 `Kconfig.defconfig` 和 `thinkpad_wireless_defconfig` 中移除了由 HWMv2 构建系统隐式指定的 SoC 及 Board 宏，消除了 Kconfig 递归冲突。

### 2.2 X220 物理按键矩阵映射与解密 (Matrix Desegregation)
*   **分析**：ThinkPad X220 键盘物理排线的矩阵排布（8 行 Sense x 16 列 Drive）在走线上属于混淆乱序排布。若在设备树的 `matrix-transform` 中进行 1 对 1 线性映射，会导致物理按键大量错乱。
*   **设计**：我们参考 `thinkpad-ec` 社区对 IBM/Lenovo 官方键盘扫描表的反编译结果（16列 Drive x 8行 Sense），在 DTS 文件的 `default_transform` 中对全部 130 个交叉按键坐标进行了完整解密映射：
    *   例如：物理 `Esc` 按键（由 Sense 5 和 Drive 0 闭合触发）被正确映射为 ZMK 键值数组的第一个索引（逻辑 `Esc`），从而实现**键盘物理按键完美归位，同时 keymap 文件保持极高可读性**。
*   **多媒体及功能键映射**：在 Row 6 键值中，完整实现了音量增减、静音、麦克风静音、翻页、前后导航键、截图及 Pause 键。

### 2.3 小红帽 (TrackPoint) 驱动与复合扫描
*   **驱动集成**：在 `west.yml` 中集成了 `tails-dev/kb_zmk_ps2_mouse_trackpoint_driver` 外部模块。在设备树中配置 `compatible = "gpio-ps2";` 总线节点，挂载 `compatible = "zmk,input-mouse-ps2";` 鼠标节点，并通过 `zmk,input-listener-ps2` 接收坐标变化并转化为 ZMK 指针报文。
*   **直连按键扫描 (Composite KSCAN)**：由于 `HOTKEY` 和 `PWRSWITCH` 是独立引脚，我们通过 `zmk,kscan-composite` 复合扫描，将主键盘 8x16 矩阵和 2 颗直连按键合二为一（设定 `row-offset = <8>`，挂载在虚拟 Row 8 上）。

### 2.4 电源灯 PWM 呼吸效果与电量监控逻辑
*   **PWM 呼吸灯**：
    *   在 `board.c` 中移除了 P0.29 的 GPIO 静态输出初始化，避免引脚冲突。
    *   在 DTS 中将 P0.29 挂载到 `pwm0` 的通道 0，定义为 `pwm_led_power`，极性设为 `PWM_POLARITY_INVERTED`。
    *   在 `status_leds.c` 线程中，利用 50 步正弦亮度表，以 80ms 为周期的 Tick 对占空比进行动态微调，实现了淡入淡出及低亮度停顿的拟真呼吸效果。
*   **充放电状态指示**：
    *   **唤醒展示**：开机/唤醒时前 5 秒根据电压检测（阈值 3.5V，对应约 10% SoC）点亮电量红灯或绿灯。
    *   **充电显示**：当检测到 USB 供电时，充电中（`CHG_INT == 0`）亮红灯，充满后亮绿灯。
    *   **极低电压保护**：电压低于 3.4V（对应约 2% SoC）时，红灯快速闪烁 5 次，随后熄灭所有指示灯和呼吸灯，执行 `sys_poweroff()` 使 nRF52840 强制进入 System OFF 休眠，整机电流降至 2µA 以免电池过放。

---

## 3. 历史更新与日志记录 (Changelog)

### [2026-07-10] v1.0.0 — HWMv2 重构与小红帽/呼吸灯/电池保护首发
1.  **目录重构**：完成 `arm` 废弃路径清理，板级文件迁移至 `config/boards/thinkpad_wireless/`。
2.  **小红帽支持**：配置 PS/2 总线与监听器，接入 `tails-dev` 指点杆驱动。
3.  **矩阵解密**：依照 `thinkpad-ec` 的 scancode 定义，重构 `default_transform`，使 X220 物理按键与 ZMK Keymap 完全映射对齐，补全了顶部音量与多媒体按键。
4.  **呼吸灯**：使用 PWM0 硬件模块驱动 P0.29，实现具有呼吸拟真质感的电源指示灯。
5.  **电池与安全**：补充开机/唤醒电量展示及低电量 5 次红灯闪烁闪警，并实现 <3.4V 强制 System OFF 极低功耗关机保护。

### [2026-07-10] v1.0.1 — 修复编译环境下的 UTF-8 BOM 冲突
1. **移除 BOM 字节**：检测到由 Windows 环境写入的 UTF-8 BOM 头（`\xef\xbb\xbf`）会导致 Linux 编译环境下的 Kconfig、yaml 编译器报错退出。对整个项目配置（`defconfig`、`west.yml`、`build.yaml`、`board.c`、`CMakeLists.txt`）进行了无损 BOM 头清除，保证编译顺利通过。

### [2026-07-10] v1.0.2 — 本地化 TrackPoint 驱动以适配 Zephyr 4.1.0
1. **移除外部模块依赖**：将 `kb_zmk_ps2_mouse_trackpoint_driver` 移出 `west.yml`，改由板级本地目录加载，增强固件独立性。
2. **修复 API 编译兼容**：
   - 将 `input_mouse_ps2.c` 中的已废弃宏 `K_THREAD_STACK_MEMBER` 替换为 `K_KERNEL_STACK_MEMBER`。
   - 将 `input_listener_ps2.c` 中的端点发送函数从 `zmk_endpoints_send_mouse_report` 修正为最新 API `zmk_endpoint_send_mouse_report`。
   - 修复 `zmk_keymap_layer_activate` 的参数传递，增加布尔型 `locking` 参数（设为 `false`）以匹配最新 ZMK 层的锁定机制。
