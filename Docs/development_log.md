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
*   **数据线 (TP4DATA)**：P1.10，带内部上拉.
*   **复位线 (TP4_RESET)**：P1.09，**高电平有效**（高=复位，低=运行；2026-08-17 CoB 实测：模块仅在 P1.09 为低时通信，拉高即永久静默，与早期固件假设的"低电平复位"相反）。

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

## 3. 编码约束与规范 (Coding Constraints)

为了防止在 Windows 环境下编辑文件引入的编码格式问题导致 Linux 编译流水线 (GitHub Actions) 报错，特制定以下编码约束：

1. **禁止包含 UTF-8 BOM 字节**：
   * 所有源文件（包括 `.c`、`.h`、`.dts`、`.yaml`、`Kconfig*`、`defconfig`、`.yml` 等）必须采用 **UTF-8 无 BOM** (UTF-8 without BOM / UTF-8) 编码保存。
   * Windows 部分编辑器（例如默认记事本或未配置的 VS Code）可能会在文件头部写入 BOM 头（十六进制字节：`EF BB BF`），这会导致 Linux 环境下的 Kconfig 预处理器 (`kconfig.py`) 或 YAML 解析器报 `unknown token at start of line` 等语法解析错误。
2. **换行符格式规范 (LF)**：
   * 为了与开源 ZMK/Zephyr 社区项目标准保持一致，使用 LF 作为换行符。Git 提交时应确保换行符能够被自动处理或保持 LF 状态。
3. **配置与验证方法**：
   * **VS Code 设置**：在项目 `.vscode/settings.json` 中配置 `"files.encoding": "utf8"`。
   * **PowerShell 快速检查与清除脚本**：
     ```powershell
     # 扫描并清除当前目录下所有文本文件的 UTF-8 BOM
     Get-ChildItem -Recurse -File -Exclude *.pdf, *.xlsx, *.png, *.jpeg, *.zip | ForEach-Object {
         $bytes = [System.IO.File]::ReadAllBytes($_.FullName)
         if ($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) {
             Write-Host "Found BOM in: $($_.FullName)"
             $newBytes = New-Object byte[] ($bytes.Length - 3)
             [System.Array]::Copy($bytes, 3, $newBytes, 0, $bytes.Length - 3)
             [System.IO.File]::WriteAllBytes($_.FullName, $newBytes)
             Write-Host "Stripped BOM successfully!"
         }
     }
     ```
4. **蓝牙设备名称长度限制**：
   * 在 `config/thinkpad_wireless.conf` 中配置 `CONFIG_ZMK_KEYBOARD_NAME` 时，字符长度**不得超过 16 个字符**。
   * ZMK 固件会在 `ble.c` 中通过 `BUILD_ASSERT(sizeof(CONFIG_ZMK_KEYBOARD_NAME) - 1 <= 16)` 强制进行静态断言校验，超出 16 字节会导致编译失败。

---

## 4. 历史更新与日志记录 (Changelog)

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

### [2026-07-10] v1.0.3 — 修复 Kconfig 预处理器宏解析错误与版本声明
1. **修复兼容性检测**：将 `Kconfig.thinkpad_wireless` 中直接使用带逗号的 compatible 字符串（如 `zmk,input-mouse-ps2`）改为先通过 `:=` 定义变量，再通过 `$(dt_compat_enabled,$(VAR))` 引用，解决 Kconfig 预处理器将逗号误判为多参数分隔符导致的 `bad number of arguments in call to dt_compat_enabled` 编译报错。
2. **固件基线版本说明**：针对升级 ZMK/Zephyr 到最新版本的要求，分析并厘清了 ZMK 主线核心与 Zephyr 4.4.1 的适配关系，确认当前采用的最优编译环境基线依然为 ZMK 官方主推的 Zephyr 4.1.0。

### [2026-07-10] v1.0.4 — 解决板级库与 ZMK 应用头文件的 CMake 作用域冲突
1. **引入应用头文件路径**：由于驱动文件和 `status_leds.c` 均被归并到了 Zephyr 板级库（`board` 静态库 target）中编译，而板级目标默认无法继承 ZMK 应用目标（`app` target）的头文件包含路径。我们在板级 `CMakeLists.txt` 中通过 `zephyr_include_directories(${CMAKE_SOURCE_DIR}/include)` 和 `${CMAKE_SOURCE_DIR}/module/include` 显式引入了 ZMK 核心头文件路径，解决了 `<zmk/endpoints.h>` 和 `<zmk/event_manager.h>` 等头文件找不到的编译报错。

### [2026-07-10] v1.0.5 — 重构板级 CMake 与 Kconfig 配置规范
1. **作用域精细控制**：将 `CMakeLists.txt` 中的全局 `zephyr_include_directories` 变更为局域 `zephyr_library_include_directories`，确保 ZMK 头文件包含路径严格隔离在板级库内部，防范多项目编译时的符号冲突和包含污染。
2. **Kconfig 净化**：移除了板级 `Kconfig.thinkpad_wireless` 中对 `config PS2` 的重复定义，以及对 `PM_DEVICE`、`BT_CTLR_ADVANCED_FEATURES` 等内核级全局符号的覆盖声明。相关配置开关已全部挪移至规范的 `thinkpad_wireless_defconfig` 中，彻底消除了 Kconfig 重定义隐患。

### [2026-07-10] v1.0.6 — 调整 GitHub Actions 为手动编译触发
1. **修改触发机制**：为了避免频繁 Push 代码导致频繁触发 GitHub Actions 云端编译，修改了 `.github/workflows/build.yml` 工作流文件。移除了 `push` 和 `pull_request` 触发器，仅保留 `workflow_dispatch` 触发器。此后，代码推送将不会触发自动编译，需要手动在 GitHub 仓库的 Actions 页面点击 "Run workflow" 按钮来启动编译。

### [2026-07-10] v1.0.7 — 修复初始化回调与底层层解构兼容问题
1. **层去激活参数修正**：在 `input_listener_ps2.c` 的 `zmk_input_listener_ps2_layer_toggle_deactivate_layer` 中，为 `zmk_keymap_layer_deactivate` 补充了第二个参数 `false` (locking 参数)，以对齐 ZMK 新版图层锁定与解锁 API 签名。
2. **SYS_INIT 回调签名对齐**：根据 Zephyr 4.x 最新规范，将 `board.c` 和 `status_leds.c` 中的 `SYS_INIT` 初始化回调函数参数签名从 `int init_fn(const struct device *dev)` 改为 `int init_fn(void)`，消除了参数不匹配的编译器警告。
3. **USB 状态检测无感封装**：删除了 `status_leds.c` 中依赖 Nordic HAL 的只读 `nrf_power_usbdetected_get` 硬件宏，使用更高层的、与硬件平台无关的 ZMK 官方 USB 状态获取函数 `zmk_usb_is_powered()` 代替，并在无 USB 设备栈配置时加入防御性降级防护，解决了在新版 nrfx 库中底层 USB 检测接口不匹配的警告。

### [2026-07-10] v1.0.8 — 全面代码审查与兼容性确认
1. **系统 API 兼容性复核**：完成 3 轮全面代码审查（Driver API、DTS & CMake、Kconfig & Dependency）。确认 `ps2_gpio.c`、`ps2_uart.c` 等底层外设驱动的 `DEVICE_DT_INST_DEFINE` 初始化回调函数参数传递形式在 Zephyr 4.x/ZMK 4.4.1 标准下结构健康且能保持向后兼容。
2. **编译路径与作用域约束**：确认 `CMakeLists.txt` 中已采用 `zephyr_library_include_directories` 精准限制了头文件作用范围，避免了自定义板级配置同 ZMK 主分支的内部路径产生全局冲突的隐患。
3. **配置宏定义对齐**：确认 `Kconfig.thinkpad_wireless` 中通过中间辅助宏以及 `dt_compat_enabled` 的结合使用，有效规避了新版 Kconfig 预处理机制导致的参数切分编译失败问题，目前整体工程代码结构已与 Zephyr 4.1.0 (ZMK 4.4.1) 规范完全对齐。

### [2026-07-10] v1.0.9 — 修复底层输入子系统回调宏参数错误
1. **INPUT_CALLBACK_DEFINE 宏签名更新**：针对 Zephyr OS (3.6+) 中 Input 子系统的 API 演进，修正了 `drivers/input_listener_ps2.c` 中报错的问题。新版本的 `INPUT_CALLBACK_DEFINE` 必须接收 3 个参数（设备指针、回调函数指针、上下文参数指针），我们已为其补充传入 `NULL`。同时，为绑定的回调函数补齐了相应的 `void *user_data` 参数，使底层宏绑定逻辑与 Zephyr 4.x (ZMK 4.4.1) 的 Input 系统完美兼容。

### [2026-07-10] v1.0.10 — 修复蓝牙名称超出系统限制的断言错误
1. **放宽最大名称长度限制并设定蓝牙名称**：由于默认 `CONFIG_BT_DEVICE_NAME_MAX`（通常为 28）无法容纳过长的默认键盘设备名，触发了 Zephyr 主机蓝牙子系统的 `BUILD_ASSERT` 静态断言。已在 `thinkpad_wireless_defconfig` 中明确设定 `CONFIG_ZMK_KEYBOARD_NAME="Thinkpad Wireless"`，并将其最大长度配置 `CONFIG_BT_DEVICE_NAME_MAX` 放宽至 `32`，双管齐下彻底解决该编译断言失败问题。

### [2026-07-10] v1.0.11 — 修复 GPIOTE 断言错误
1. **显式启用 gpiote 节点**：在 Zephyr 4.x/nrfx 的新版校验规则下，如果 GPIO 端口配置了中断触发支持，就必须确保底层的 `gpiote` 实例处于激活状态。因默认设备树中该节点未默认开启，导致了 `gpio_nrfx.c` 触发 `BUILD_ASSERT` 编译失败。我们在 `thinkpad_wireless.dts` 的尾部追加了 `&gpiote { status = "okay"; };`，显式激活该模块，彻底解决此静态断言错误。

### [2026-07-10] v1.0.12 — 规范板级文件迁移至 HWMv2
1. **ZMK 主线版本恢复**：将 `config/west.yml` 中的 ZMK 核心库版本恢复为 `main`，以启用 Zephyr 4.1.0 所支持的 HWMv2 机制。
2. **模块板级路径注册**：修改 `zephyr/module.yml` 将 `board_root` 设置为 `module`，使 Zephyr 能在模块目录下自动查找板级定义。
3. **板级目录嵌套规范化**：将板级文件目录移动至 `module/boards/thinkpad/thinkpad_wireless/`，并遵循 HWMv2 的 `boards/<vendor>/<board_name>` 的厂商嵌套命名规范。
4. **Kconfig 文件重命名**：将 `Kconfig.board` 重命名为 `Kconfig.thinkpad_wireless`，以满足 HWMv2 的自动搜集命名要求。
5. **CMake 引入相对路径修复**：由于增加了一级厂商嵌套目录，将 `CMakeLists.txt` 中的全局应用头文件路径更新为 `../../../include`。
6. **自动化流水线触发机制**：为 `.github/workflows/build.yml` 的推送和 PR 事件追加了路径过滤触发规则，确保后续代码变更能自动校验编译。

### [2026-07-10] v1.0.13 — 缩短蓝牙设备名以满足 ZMK 静态断言限制
1. **短名称调整**：ZMK 官方固件在 `app/src/ble.c` 中强制要求蓝牙设备名称（`CONFIG_ZMK_KEYBOARD_NAME`）的长度不得超过 16 个字符，即触发 `BUILD_ASSERT(sizeof(CONFIG_ZMK_KEYBOARD_NAME) - 1 <= 16)` 静态校验。先前设置的 `"Thinkpad Wireless"` 包含空格共 17 个字符，导致编译断言报错。已将其缩短为 `"ThinkpadWireless"`（共 16 字符），顺利通过固件编译阶段。


### [2026-07-10] v1.0.14 — 修复 ADC 禁用导致的 battery_voltage_divider 编译失败与设备树警告
1. **显式启用 ADC 外设节点**：在 `thinkpad_wireless.dts` 设备树中显式追加并启用了 `&adc` 节点（`status = "okay"`）。在 Zephyr 4.x 构建下，由于默认 `nrf52840.dtsi` 中 `adc` 状态为 `"disabled"`，且未定义启用该节点，导致 ZMK 的 `battery_voltage_divider.c` 无法探测到有效的 ADC 设备而触发 `#error Unsupported ADC` 错误。使能该节点后成功生成 `DT_HAS_NORDIC_NRF_SAADC_ENABLED` 符号并恢复正常编译。
2. **显式指定 SAADC 驱动**：在 `thinkpad_wireless_defconfig` 中显式指定 `CONFIG_ADC_NRFX_SAADC=y` 以对齐底层 Nordic 外设驱动依赖。
3. **消除 Devicetree 警告**：将 `thinkpad_wireless.dts` 中 `kscan_composite` 复合扫描节点下的已废弃属性 `column-offset` 变更为符合新版规范的 `col-offset`，消除了 `'column-offset' is marked as deprecated` 的编译警告。


### [2026-07-10] v1.0.15 — 解决自定义模块无法继承 ZMK 应用头文件路径的编译报错
1. **添加应用头文件路径**：在 `module/CMakeLists.txt` 中显式追加了 `zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)` 包含路径。自定义模块（`module` 静态库 target）在编译外设驱动与输入监听器（如 `input_listener_ps2.c`）时，由于作用域隔离默认无法访问 ZMK 的应用级头文件，引发无法找到 `<zmk/endpoints.h>` 和 `<zmk/event_manager.h>` 等核心头文件的报错。引入该路径后成功解决了头文件包含失败的问题。


### [2026-07-10] v1.0.16 — 解决自定义 DTS 绑定路径未注册导致编译宏未定义的问题
1. **注册自定义 DTS 路径**：在 `zephyr/module.yml` 的 `settings` 块中显式指定了 `dts_root: module`。在 HWMv2 重构中，自定义模块下的 Devicetree 绑定配置（如 `gpio-ps2.yaml` 和 `zmk,input-mouse-ps2.yaml`）在没有指定 `dts_root` 搜索根路径的情况下，无法被 Zephyr 的 DTS 解析器发现和识别，这导致相关驱动源文件（`input_mouse_ps2.c` 和 `ps2_gpio.c`）在编译时因为设备树节点无法生成正确的 phandle 依赖宏而报 `__device_dts_ord_...` 未定义的编译错误。注册后成功解决此问题。


### [2026-07-10] v1.0.17 — 修复 USBD 禁用导致的 USB 协议栈链接未定义引用报错
1. **显式启用 USBD 外设节点**：在 `thinkpad_wireless.dts` 设备树中显式追加并启用了 `&usbd` 节点（`status = "okay"`）。在 Zephyr/ZMK 构建下，由于默认的 `nrf52840.dtsi` 中 `usbd` 状态为 `"disabled"` 且未在板级 DTS 中开启，导致 ZMK 的 USB 设备协议栈被关闭。这引发了 ZMK 应用核心逻辑（`indicator_leds.c` 等）在链接阶段报出对 `zmk_usb_get_conn_state` 和 `zmk_event_zmk_usb_conn_state_changed` 的 `undefined reference` 未定义引用报错。使能该物理控制器后解决了该链接错误。

### [2026-07-14] v1.01 (Upgrade) 一 ZMK Studio 物理布局集成、开关机软按键锁定及 LED 顺序点亮动画
1. **ZMK Studio 物理布局支持 (Physical Layout)**：在物理布局分离文件 `thinkpad_wireless-layouts.dtsi` 中定义了符合 ZMK Studio 可视化键盘编辑器规范的 `zmk,physical-layout` 属性节点，并在板级设备树 `thinkpad_wireless.dts` 的 `chosen` 节点中注册了 `zmk,physical-layout = &physical_layout0;`，使得固件完全兼容最新的 ZMK Studio 键图编辑能力。
2. **待机功耗指示灯优化**：在 `status_leds.c` 中订阅了 ZMK 的活动状态变化事件 `zmk_activity_state_changed`。当主控检测到键盘进入闲置状态时（`ZMK_ACTIVITY_IDLE` 或 `ZMK_ACTIVITY_SLEEP`），强制关闭蓝牙状态灯（`BT_LED`）与电量指示灯，仅保留主电源 LED 的 PWM 呼吸效果，从而节省待机时的非必要电量。
3. **紧急关机外设供电切断保护**：在电池极低电压紧急关机代码中，在调用 `sys_poweroff()` 之前，显式将 5V 使能控制引脚 `P0.12` (`5V_EN`) 配置为输出并拉低电平（0V）。这彻底关断了 5V 升压电路（ETA1061），完全切断了小红帽的供电，避免了主控进入 System OFF 后由于引脚悬空导致升压芯片继续工作从而过放电池的重大隐患。
4. **保留寄存器软开关机锁定与防误触重入休眠**：
   * **开机校验（长按 2 秒）**：在系统极早期启动阶段（`PRE_KERNEL_2`，`board.c`），若读取到 GPREGRET 寄存器中存有手动关机标志值 `0xAA`，则对 `PWRSWITCH` 按键（`P1.11`）进行长按检测。如果不是按键唤醒（误触）或长按未满 2 秒，主控会在数毫秒内瞬间拉低 `5V_EN` 并重新进入 System OFF，完美防范了在包里因键位受压误触开机的问题。长按满 2 秒则清除标志并闪烁绿色电量灯进行开机指示。
   * **关机逻辑（长按 8 秒）**：在 LED 线程中监测电源按键状态，长按满 8 秒时在 `NRF_POWER->GPREGRET` 写入手动关机标志 `0xAA`，随后熄灭所有外设供电并进入 System OFF。
5. **开关机 LED 动画设计**：
   * **开机**：长按开机成功后，所有指示灯（BT灯、绿/红电量灯、静音灯、麦克风静音灯、Caps Lock灯）呈 150ms 间隔顺序点亮，并在全亮 300ms 后全部熄灭，交由 ZMK 系统正常控制。
   * **关机**：长按关机触发后，所有指示灯同时常亮 300ms 作为警示，随后呈 150ms 间隔顺序熄灭。
6. **过时头文件引用规范清理**：在驱动文件 `behavior_mouse_setting.c` 中将过时的 `#include <drivers/behavior.h>` 更新为现代命名空间风格的 `#include <zephyr/drivers/behavior.h>`。


### [2026-07-14] v1.0.18 一 集成 CodeQL 与 Cppcheck 静态扫描并修复代码漏洞
1. **GitHub Actions 静态分析集成**：
   * **CodeQL 漏洞扫描**：新增了 [.github/workflows/codeql.yml](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/.github/workflows/codeql.yml) 配置文件，使用免编译的 `build-mode: none` 运行 CodeQL，对项目中的自定义 C 代码进行深度的安全分析与漏洞扫描。
   * **Cppcheck 静态检查**：在 [.github/workflows/lint.yml](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/.github/workflows/lint.yml) 中集成了 Cppcheck 静态分析工具，配置参数为 `--enable=warning,portability`，仅在发现真实 bug 和可移植性问题时拦截构建，规避普通代码风格建议（Style）导致的误报，并同时在并行的 style-check 任务中进行编码格式（BOM/LF）和 clang-format 的校验。
2. **修复 Cppcheck 发现的代码 bug**：
   * **修复错误捕获失效漏洞**：在 [input_mouse_ps2.c](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/drivers/input_mouse_ps2.c) 的 `zmk_mouse_ps2_init_thread` 线程初始化逻辑中，修复了调用 `zmk_mouse_ps2_set_sampling_rate` 但未将其返回值赋值给 `err` 变量的逻辑 Bug（导致后面的错误检测无效化），修正为 `err = zmk_mouse_ps2_set_sampling_rate(...)`。
   * **修复 printf 格式不匹配与 typo 警报**：在 [input_mouse_ps2.c](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/drivers/input_mouse_ps2.c) 的 `zmk_mouse_ps2_send_cmd` 函数中，修正了 size_t 类型的 `sizeof` 打印时未匹配 `%zu` 格式字符的警告，并修正了拷贝粘贴导致的 typo 笔误（应打印限制缓冲区的大小 `sizeof(resp.resp_buffer)`，而非 `sizeof(resp.err_msg)`）。
### [2026-08-04] v1.0.19 — 构建产物格式修复（启用 UF2/HEX 输出，解决烧录兼容问题）
1. **问题背景**：Release v1.1 的固件资产仅有 `thinkpad_wireless-zmk.bin`（raw binary），无法被 nRF Connect for Desktop 直接烧录。原因有二：
   * ZMK 官方 CI 流程（`build-user-config.yml`）的产物逻辑为：若构建目录存在 `zephyr.uf2` 则发布 `.uf2`，否则 fallback 发布 `.bin`。本板 defconfig 未启用 UF2 输出，故只产出了 `.bin`。
   * raw binary 不含地址信息，nRF Connect Programmer 加载时默认按 `0x0000` 起始烧录，会覆盖 `0x0-0x26000` 区域的 Adafruit bootloader 向量表头，导致芯片上电后既无法进入 bootloader 也无法启动应用（变砖）。手工改名 .hex 无效（文件内容格式未变）。
2. **修改点**：在 `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless_defconfig` 中追加：
   ```kconfig
   CONFIG_BUILD_OUTPUT_UF2=y
   CONFIG_BUILD_OUTPUT_HEX=y
   ```
   与 ZMK 官方 nRF52840 参考板（如 nice_nano）的 defconfig 保持一致。
3. **预期效果**：
   * GitHub Actions 将产出 `thinkpad_wireless-zmk.uf2`：按住 BOOT 键插入 USB，拖拽即完成烧录（无需调试器）。
   * GitHub Actions 将产出 `thinkpad_wireless-zmk.hex`：Intel HEX 自带 `0x26000` 起始地址，nRF Connect Programmer 按地址写入应用分区，不触碰 `0x0` 保留区与 `0xF4000` 的 bootloader 区。

### [2026-08-04] v1.0.20 — 固件产物收敛为 HEX 格式（关闭 UF2 输出）
1. **问题背景**：v1.0.19 启用 `CONFIG_BUILD_OUTPUT_UF2=y` 后，GitHub Actions 产物仅包含 `.uf2`。原因：ZMK 官方 `build-user-config.yml` 的产物复制逻辑为 if/elif 二选一——存在 `zmk.uf2` 时只发布 `.uf2`，不会复制 hex/bin。而本项目的烧录方案为 J-Link + nRF Connect Programmer，需要的是 Intel HEX。
2. **修改点**：
   * `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless_defconfig`：移除 `CONFIG_BUILD_OUTPUT_UF2=y`，保留 `CONFIG_BUILD_OUTPUT_HEX=y`。
   * `.github/workflows/build.yml`：向 ZMK 官方 workflow 传入 `fallback_binary: hex`，使无 UF2 产物时发布 `thinkpad_wireless-zmk.hex`。
   * `.github/workflows/release.yml`：同步传入 `fallback_binary: hex`，保证 tag 发布时 release 资产同样为 `.hex`（release 的 files 匹配模式已含 `*.hex`）。
3. **预期效果**：CI 与 Release 产物统一为 Intel HEX（起始地址 `0x26000`，nRF Connect Programmer 可直接烧录，注意取消 "Erase all" 以免擦除 bootloader 区）。

### [2026-08-04] v1.0.21 — 启用 USB HID 有线输出（修复 Windows 识别为"其他设备"）
1. **问题背景**：烧录完整镜像（bootloader + 应用）后连接电脑，Windows 将设备识别为"其他设备"且无法安装驱动，键盘无法作为有线 HID 键盘使用。排查发现固件**从未启用 USB HID 输出**：
   * `config/thinkpad_wireless.conf` 缺少 `CONFIG_ZMK_USB=y`。ZMK 中 USB 为显式开关，该配置会 `select USB` / `select USB_DEVICE_STACK` / `select USB_DEVICE_HID`；未启用时应用固件不包含 HID 键盘接口，USB 枚举无有效接口描述符。
   * `thinkpad_wireless.dts` 的 `chosen` 缺少 `zephyr_udc0` 标签（等价于 `chosen { zephyr,udc0 = &usbd; };`），Zephyr 的 `usb_enable()` 无法定位 USB 设备控制器。对比 ZMK 官方参考板（nice_nano）均有 `zephyr_udc0: &usbd { status = "okay"; };`。
2. **修改点**：
   * `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless.dts`：将裸 `&usbd` 节点改为 `zephyr_udc0: &usbd`（注册为 Zephyr USB 设备控制器 chosen）。
   * `config/thinkpad_wireless.conf`：追加 `CONFIG_ZMK_USB=y`。VID/PID（0x1D50:0x615E）与厂商名 "ZMK Project" 采用 ZMK 默认值，无需另行配置。
3. **预期效果**：固件枚举出标准 USB HID 键盘接口，Windows/macOS/Linux 免驱即插即用；`status_leds.c` 中的 `zmk_usb_is_powered()` 在 USB 接入时正确上报 VBUS 状态（充电指示灯逻辑生效）。

### [2026-08-04] v1.0.22 — 启用 ZMK Studio（USB CDC + BLE 双传输通道，暂不加解锁键）
1. **问题背景**：用户在 https://zmk.studio/ 无法连接设备。原因：固件未编译 ZMK Studio 支持（`CONFIG_ZMK_STUDIO` 未启用），且缺少 USB CDC-ACM 传输端点。
2. **修改点**：
   * `config/thinkpad_wireless.conf`：追加 `CONFIG_ZMK_STUDIO=y`。BLE GATT 传输（`ZMK_STUDIO_TRANSPORT_BLE`）随 ZMK_BLE 自动启用；RPC 自动引入 `ZMK_BEHAVIOR_METADATA`、`ZMK_KEYMAP_SETTINGS_STORAGE` 等依赖。
   * `build.yaml`：为 `thinkpad_wireless` 板添加官方 `studio-rpc-usb-uart` snippet。该 snippet 自动完成：在 `&zephyr_udc0`（v1.0.21 已注册）下挂载 `zephyr,cdc-acm-uart` 节点、写入 `chosen { zmk,studio-rpc-uart = ...; }`、启用 `CONFIG_USB_CDC_ACM` / `SERIAL` / `UART_INTERRUPT_DRIVEN` / `UART_LINE_CTRL`，并定义 `ZMK_BEHAVIORS_KEEP_ALL` 使全部行为可用。
3. **传输通道汇总**：
   * **USB**：HID 键盘接口（v1.0.21）+ CDC-ACM 串口（Studio RPC）。
   * **蓝牙**：HID over GATT + Studio GATT RPC。
   * 输出切换：ZMK 默认 USB 优先，USB 断开自动切 BLE。
4. **暂缓项**：按用户要求暂不在 keymap 中绑定 `&studio_unlock` 解锁键。注意：ZMK Studio 默认锁定（`ZMK_STUDIO_LOCKING`），未解锁前 Studio 可连接但不可编辑键位；后续需要时在 keymap 绑定 `&studio_unlock` 行为即可解锁。
5. **遗留待办**：physical-layout 节点尚无 `keys` 属性（键位物理坐标），ZMK Studio 可视化编辑可能受限，后续需在 `thinkpad_wireless-layouts.dtsi` 补充。

### [2026-08-04] v1.0.23 — 修复 ZMK Studio 编译失败（移除 matrix-transform chosen + 补全 physical-layout keys）
1. **编译错误现象**：启用 `CONFIG_ZMK_STUDIO` 后构建失败。根因是 ZMK 源码 `app/src/physical_layouts.c` 的两处编译期断言：
   * `BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_STUDIO) || USE_PHY_LAYOUTS, ...)`：`USE_PHY_LAYOUTS = 有 zmk,physical-layout 节点 && !DT_HAS_CHOSEN(zmk_matrix_transform)`。板级 `chosen` 中保留了 `zmk,matrix-transform = &default_transform;`，触发断言失败。ZMK Studio 要求键盘以 physical layout 为唯一布局来源，不得再指定 chosen matrix-transform。
   * `BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_STUDIO) || DT_INST_NODE_HAS_PROP(n, keys), ...)`：physical layout 节点必须定义 `keys` 属性（每个键的物理尺寸/坐标），原 `thinkpad_wireless-layouts.dtsi` 仅有 transform/kscan，无 keys。
2. **修改点**：
   * `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless.dts`：从 `chosen` 移除 `zmk,matrix-transform = &default_transform;`（`default_transform` 节点保留，由 physical layout 的 `transform` 属性引用）。
   * `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless-layouts.dtsi`：`#include <physical_layouts.dtsi>` 引入 `&key_physical_attrs` 节点，并按 transform map 的 130 个 position 顺序定义 `keys` 数组（width/height/x/y，单位 1U=100），键位语义与 `config/thinkpad_wireless.keymap` 绑定顺序一一对应（Esc/F 行、数字行、Tab 行、Caps 行、Shift 行、底行、媒体行、direct 键）。
3. **说明**：keymap 文件仅包含键值绑定（无物理坐标）；物理矩阵（RC→position）定义于 dts 的 `default_transform`，physical layout 的 keys 顺序与之一致。键位坐标按 X220 经典 7 行布局近似排布，后续可在 Studio 可视化界面微调。

### [2026-08-04] v1.0.24 — 修复 layouts.dtsi keys 数组 DTS 语法错误
1. **编译错误**：`thinkpad_wireless-layouts.dtsi:15: parse error: expected '{', '=', or ';'`。根因：生成的 `keys` 数组首个元素前带前导逗号（`, <&key_physical_attrs ...>`），且属性后缺少赋值符号 `=`。DTS 语法要求：属性 `keys` 后跟 `= <...>`（首元素），续行用 `, <...>` 分隔，最后以 `;` 结束（对照 ZMK 官方 physical-layout 示例，如 tester_pro_micro-layouts.dtsi）。
2. **修改点**：`module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless-layouts.dtsi` 中首个 keys 元素的 `, <&key_physical_attrs` 改为 `= <&key_physical_attrs`，其余 129 行保持 `, <...>` 不变，130 项总数不变。

### [2026-08-04] v1.0.25 — 修复固件链接地址（补 CONFIG_USE_DT_CODE_PARTITION）
1. **问题发现**：CI 产出的 `thinkpad_wireless-zmk.hex` 数据从 `0x0` 开始（向量表在 0x0），而非预期的 `0x26000`。对比 ZMK 官方板（nice_nano 的 `nice_nano_2_0_0_defconfig`）发现其包含 `CONFIG_USE_DT_CODE_PARTITION=y`，而本板 defconfig 缺失。
2. **影响分析**：Zephyr 的 `FLASH_LOAD_OFFSET` 仅在 `CONFIG_USE_DT_CODE_PARTITION=y` 时取 `chosen/zephyr,code-partition` 的地址（0x26000），否则默认为 0。缺失该配置导致**所有历史固件（含 Release v1.1 的 bin）实际链接在 0x0**，与 Adafruit bootloader 的 0x26000 应用偏移布局不兼容：
   * 此前按 0x26000 偏移转换/烧录的方式放错了位置；
   * 烧录后 bootloader 跳转 0x26000 的伪应用，代码内部引用 0x0 基址的绝对地址，导致设备无法作为键盘工作（表现为"其他设备"）。
3. **修改点**：`module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless_defconfig` 追加 `CONFIG_USE_DT_CODE_PARTITION=y`，使应用链接到 `code_partition`（0x26000），与 ZMK nRF52840 官方板布局一致。
4. **预期效果**：新构建的 hex 数据起始地址为 0x26000；配合 bootloader（0x0 引导头 + 0xF4000 主体）与完整镜像烧录方案，键盘方可正常启动工作。

### [2026-08-04] v1.0.26 — GitHub Actions 产出完整烧录镜像（bootloader + 应用合并）
1. **需求**：CI 产物（`thinkpad_wireless-zmk.hex`）仅为应用固件，不含 bootloader；要求 Actions 直接生成可一步全片烧录的完整镜像。
2. **修改点**：
   * 新增 `tools/merge_hex.py`：通用 Intel HEX 合并脚本（支持 type 02/04 地址记录、跨 64KB 边界、UICR 区域，自动检测地址重叠）。用法：`python3 tools/merge_hex.py <输出> <输入1> <输入2> ...`。
   * 新增 `firmware/adafruit_bootloader_nosd.hex`：Adafruit nRF52 Bootloader（ZMK 变体、无 softdevice，应用偏移 0x26000），来源 joric/nrfmicro release 1.4（`nrfmicro_nrf52840_bootloader-0.8.0-dirty_nosd.hex`），附 `firmware/README.md` 说明来源与覆盖区域。
   * `.github/workflows/build.yml`：新增 `create-full-image` job（依赖官方 build），下载应用 hex 后合并 bootloader 与应用，产出 `firmware-full` artifact（`thinkpad_full_0x0000.hex`，覆盖 0x0 起完整布局）。
   * `.github/workflows/release.yml`：release job 中合并完整镜像，release 资产包含 `thinkpad_full_0x0000.hex` 与 `thinkpad_wireless-zmk.hex`。
3. **烧录说明**：完整镜像用于全片重建（nRF Connect Programmer 勾选 "Erase all"）；应用 hex 用于仅更新应用（不擦除，bootloader 保留）。

### [2026-08-04] v1.0.27 — 更换为 ZMK 布局 bootloader（修复上电停留在 DFU 模式）
1. **问题现象**：烧录完整镜像后，插入 USB 弹出 nRFmicro 升级盘（bootloader DFU 模式），无键盘/鼠标设备，ZMK Studio 连接 COM 失败。
2. **根因分析**（通过反汇编对比两个 bootloader 的 DFU_BANK_0_REGION_START 常量）：
   * `DFU_BANK_0_REGION_START = CODE_REGION_1_START = is_sd_existed() ? SD_SIZE_GET(MBR_SIZE) : MBR_SIZE`。
   * 原使用的 **nrfmicro nosd 变体**（`nrfmicro_nrf52840_bootloader-0.8.0-dirty_nosd.hex`）代码中**无 0x26000 常量**：无 SoftDevice 时应用起始地址为 **MBR_SIZE=0x1000**，与 ZMK 的 0x26000 布局不兼容 → `bootloader_app_is_valid()` 判定 0x26000 的应用无效 → 停留 DFU 弹升级盘。
   * **nice_nano bootloader 0.6.0**（`nice_nano_bootloader-0.6.0_s140_6.1.1.hex`）代码中含 0x26000 常量（0x1384/0x3008/0x2577C/0x257FC），且 hex 内置 SoftDevice（0x1000-0x25DE8）→ bootloader 检测到 SD → 应用地址正确指向 0x26000。
3. **修改点**：
   * `firmware/adafruit_bootloader_nosd.hex`（废弃，删除）→ 新增 `firmware/adafruit_bootloader_zmk.hex`（nice_nano 0.6.0 s140 版，ZMK 布局）。
   * `.github/workflows/build.yml`、`.github/workflows/release.yml`：bootloader 引用更新为 `firmware/adafruit_bootloader_zmk.hex`。
   * `firmware/README.md`：更新来源说明，并注明勿用 nrfmicro nosd 变体。
4. **预期效果**：完整镜像 = bootloader 引导头（0x0）+ SoftDevice（0x1000-0x26000）+ ZMK 应用（0x26000）+ bootloader 主体（0xF4000）+ UICR；bootloader 校验通过后跳转应用，键盘正常启动。

### [2026-08-04] v1.0.28 — 完整镜像自动写入 bootloader settings 有效标记（解决 J-Link 直烧弹 U盘）
1. **问题现象（最终根因）**：J-Link 直接烧录完整镜像后，上电弹 U盘（UF2 bootloader 模式，卷标 NICENANO/INFO_UF2.TXT），键盘无法启动。排查链路：
   * 芯片内容（bootloader + SD + 应用）逐字节正确；MBR、UICR.BOOTSTART（0x10001014=0xF4000）、is_sd_existed（0x3004=0x51B1E5DB）、settings 页（0xFF000=FF）均正常。
   * 反汇编确认 bootloader 的 `bootloader_app_is_valid()` 走 settings 判定；**J-Link 直烧时 settings 页保持擦除态，缺少 `bank_0 = BANK_VALID_APP` 标记**（正常 UF2 拖拽流程中 bootloader 烧完固件会自动写入该标记）。手动写入 `0xFF000 = 0x0001` 后，bootloader 判定应用有效，键盘/鼠标/ZMK Studio 全部恢复正常。
2. **修改点**（无需修改任何固件代码，仅镜像生成流程）：
   * `tools/merge_hex.py`：新增 `--mark-app-valid` 选项，合并完整镜像时在 settings 页（0xFF000）写入 `bank_0 = 0x0001 (BANK_VALID_APP)`、`bank_0_crc = 0x0000`（禁用 CRC 检查）、`bank_0_size = 应用大小`。
   * `.github/workflows/build.yml`、`.github/workflows/release.yml`：完整镜像合并命令追加 `--mark-app-valid`。
   * `firmware/README.md`：补充烧录说明。
3. **烧录注意事项**：完整镜像烧录须勾选 "Erase all"（擦除 settings 页后再写入标记，flash 仅支持 1→0 写入）。

### [2026-08-05] v1.0.29 — 键盘矩阵 KSCAN 引脚极性与开漏扫描逻辑修正（匹配 X220 原理图）
1. **问题现象**：启动后键盘无响应，或全盘键位在无任何按压时产生大量 `pressed: true` 幽灵误触发；修正引脚上拉后出现仅键盘右侧少数按键有响应、左侧及中部大部分按键无响应的现象。
2. **根因分析**：
   * 原 DTS 中配置为 `diode-direction = "col2row";` 且 `row-gpios` 设为下拉 `(GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN)`。ThinkPad X220 官方主板原理图中，SENSE (行脚) 硬件上拉至 3.3V，二极管阳极在 SENSE 侧，导致高电平驱动被二极管反向截止。
   * 当将 `row-gpios` 改为上拉 `(GPIO_ACTIVE_LOW | GPIO_PULL_UP)` 后，若错误地将 `diode-direction` 改为 `"row2col"`，ZMK 会把 `col-gpios` 拿去当作输入脚 (Inputs) 读取。而 `col-gpios` 悬空 (Floating) 导致上电电平被 `GPIO_ACTIVE_LOW` 误判为全盘被按压。
   * 当保持 `diode-direction = "col2row"` 且 `col-gpios` 设为推挽输出时，未被扫描的 15 列在主动输出 3.3V 高电平，高电平电流通过内部线路倒灌至 SENSE 线上，导致 SENSE 线的电压无法被目标扫描列拉低至 0.9V 逻辑低电平门限以下（表现为仅右侧少数列正常，左侧大部分按键无响应）。
3. **修改点**：
   * `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless.dts`：
     - `matrix_kscan` 保持 `diode-direction = "col2row";`（保证 Row 为 SENSE 输入、Col 为 DRV 输出）；
     - `row-gpios` 配置为 **`(GPIO_ACTIVE_LOW | GPIO_PULL_UP)`**（3.3V 上拉输入，低电平有效）；
     - `col-gpios` 配置为 **`(GPIO_ACTIVE_LOW | GPIO_OPEN_DRAIN)`**（开漏驱动模式，扫描时拉低，未选中时高阻抗抗倒灌）。
4. **验证效果**：矩阵扫描完全正常，全键盘键位（QWERTY 字母、数字行、功能键等）按下与释放均可精准响应。

### [2026-08-05] v1.0.30 — PS/2 指点杆 (TrackPoint) 低电平复位逻辑修复（解决 Reset 锁死 0.2V 问题）
1. **问题现象**：串口频繁报错 `ps2_gpio: Failed to write value 0xff at pos=1: scl timeout`，用万用表实测 `TP4_RESET` (P1.09) 引脚电压仅有 0.2V。
2. **根因分析**：在 `module/drivers/input_mouse_ps2.c` 的 `zmk_mouse_ps2_init_power_on_reset` 函数中，代码清除了设备树标志 (`data->rst_gpio.dt_flags = 0;`)，并在 600ms 复位延时结束后误调用 `gpio_pin_set_dt(&data->rst_gpio, 0)` 将 P1.09 强行锁死在低电平 0V (实际测到 0.2V)。指点杆模块为低电平复位 (Active Low Reset)，复位完成后需要维持在 **3.3V 高电平**才能正常启动。引脚被永久锁死在 0.2V 导致小红帽芯片被一直按在强制复位状态，无法响应任何 PS/2 时钟和数据信号。
3. **修改点**：
   * `module/drivers/input_mouse_ps2.c`：修正 `zmk_mouse_ps2_init_power_on_reset`，移除强行清零 `dt_flags` 的逻辑；上电复位开始时输出 0V (LOW) 维持 600ms 脉冲，复位结束后通过 `gpio_pin_set_raw(data->rst_gpio.port, data->rst_gpio.pin, 1)` 释放并保持为 **3.3V 高电平 (HIGH)**。
4. **验证效果**：P1.09 顺利恢复为 3.3V 高电平，指点杆芯片正常退出复位模式。


### [2026-08-12] v1.0.31 — 硬件更新：BMD-340 模块设计（引脚重映射至标准驱动 GPIO）
1. **背景**：硬件设计改用 u-blox **BMD-340** 模块（内置 PCB 天线；与 BMD-341 仅天线形态不同，footprint 完全兼容，原理图与网表见 `Hardware/BMD 340SCH.pdf` / `BMD 340SCH.tel`）。BMD-34x 数据手册将 GPIO 分为两类：**标准驱动引脚**（支持 >10kHz 信号）与**受限引脚**（`P1.01-P1.07`、`P1.10-P1.15`、`P0.02/03/09/10/28-31`，标注 "Standard drive, low frequency I/O only (<10kHz)"）。原 PCB 设计中 PS/2 时钟/数据及矩阵列驱动恰好落在受限引脚上，超出其频率能力，必须重映射。
2. **引脚重映射表**（`thinkpad_wireless.dts` / `board.c` / `status_leds.c`）：
   | 信号 | 原引脚（受限） | 新引脚（标准驱动） | 说明 |
   |---|---|---|---|
   | TP4CLK（PS/2 时钟） | P1.13 | **P0.06**（模块 pin 22） | PS/2 时钟 10~16.7kHz，需标准驱动 GPIO |
   | TP4DATA（PS/2 数据） | P1.10 | **P0.11**（模块 pin 27） | 同上 |
   | DRV15（矩阵第 15 列驱动） | P1.03 | **P0.08**（模块 pin 24） | 列扫描输出，需标准驱动 GPIO |
   | CHG_INT（充电状态） | P0.08 | **P1.03**（模块 pin 59） | 静态低频输入，移至受限引脚，与 DRV15 互换 |
3. **修改点**：
   * `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless.dts`：`scl-gpios` = `P0.06`、`sda-gpios` = `P0.11`；矩阵 `col-gpios` 第 16 列 (DRV15) 改为 `P0.08`（`GPIO_DS_ALT_LOW/HIGH` 14mA 高驱动配置保留）。
   * `module/boards/thinkpad/thinkpad_wireless/board.c`：CHG_INT 初始化由 `gpio0 pin 8` 改为 `gpio1 pin 3`（上拉输入）。
   * `module/boards/thinkpad/thinkpad_wireless/status_leds.c`：`CHG_INT_PIN` 宏由 `8 (gpio0)` 改为 `3 (gpio1)`，充放电状态读取随引脚迁移。
4. **验证效果**：固件与 BMD-340 模块原理图网表对齐；高频信号（PS/2、列驱动）全部落在标准驱动引脚上，符合 BMD-34x 电气规范，避免受限引脚低频驱动能力不足导致的信号完整性问题。

### [2026-08-12] v1.0.32 — 分支体系整理（bmd340-module 转正为默认分支，清理冗余远端分支）
1. **背景**：BMD-340 模块化设计（BMD340-module 分支，基于 deepseekV4 增加 2 个提交：BMD-340 原理图/网表文档与引脚重映射）验收通过，决定将其转正为项目主分支。
2. **Git 仓库整理**：
   * 修复 `.git/config` 中错误的 fetch refspec（原先被限制为仅跟踪 `zmk-official-hwmv2-fix` 单分支，导致 `git fetch` 无法同步服务器上的其他分支），恢复为标准的 `+refs/heads/*:refs/remotes/origin/*` 通配规则。
   * 推送 `bmd340-module` 至 GitHub，并通过 API 将仓库默认分支（原 `zmk-official-hwmv2-fix`）切换为 `bmd340-module`。
   * 删除 GitHub 远端冗余分支：`main`、`zmk-official-hwmv2-fix`、`zmk-official`；同步清理本地 `zmk-official-hwmv2-fix` 分支及失效的远端跟踪引用（`origin/HEAD` 已自动更新指向 `origin/bmd340-module`）。
3. **保留分支**：`deepseekV4`（nRF52840 14mA High Drive 调试分支）保留，以 git worktree 方式挂在项目内 `.worktrees/deepseekV4` 并行维护，与主分支互不干扰；`.worktrees/` 已加入 `.gitignore`。
4. **最终分支状态**：远端与本地均仅保留 `bmd340-module`（默认分支，6053a66）与 `deepseekV4`（374e4be）两个分支。

### [2026-08-12] v1.0.33 — 硬件 v2 与代码同步（DRV4/DRV13 交换至标准引脚，模块型号确认为 BMD-340）
1. **背景**：v1.0.31 的引脚重映射只覆盖了 PS/2 与 DRV15，仍有 **DRV4 (P1.01)** 与 **DRV13 (P1.05)** 两个列驱动落在受限引脚（u-blox "Standard drive, low frequency I/O only (<10kHz)"，不保证高驱动档）。列驱动需 `GPIO_DS_ALT_LOW/HIGH` 14mA 高驱动输出，同列多键同按（≥3 键，约 0.75mA）时受限引脚可能出现灌电流不足。原理图更新为 v2，采用就近交换方案将 16 个列驱动全部移至标准驱动引脚。
2. **硬件改动**（`Hardware/BMD 340SCH.pdf` / `.tel`，v2，diff 仅 4 个网络变化）：
   | 信号 | 原引脚 | 新引脚（模块 pin） | 引脚性质 | 说明 |
   |---|---|---|---|---|
   | DRV4（列驱动） | P1.01（受限） | **P0.26**（pin 7，标准） | 受限→标准 | 与 SENSE0 交换 |
   | DRV13（列驱动） | P1.05（受限） | **P1.09**（pin 52，标准） | 受限→标准 | 与 TP4_RESET 交换 |
   | SENSE0（行输入） | P0.26（标准） | **P1.05**（pin 48，受限） | 标准→受限 | 矩阵行输入，kHz 级低频扫描，受限引脚合规 |
   | TP4_RESET（PS/2 复位） | P1.09（标准） | **P1.01**（pin 57，受限） | 标准→受限 | 静态复位信号（上电脉冲后保持高电平），受限引脚合规 |
   - **模块型号确认**：实际使用 **BMD-340-A-R**（内置 PCB 天线），非 BMD-341（U.FL 外接天线）。两者 footprint 完全一致（68-pin LGA，15.0×10.2×1.9mm，datasheet 原文 "The BMD-341 footprint is identical to the BMD-340"），引脚映射无差异，仅天线形态不同：BMD-340 需 PCB 天线净空区（上下无铜 + 下方接地平面），BMD-341 需 U.FL 座装配空间。原理图器件名 `BMD-341-A-R` 与 BOM 需更正为 `BMD-340-A-R`。
3. **代码改动**（`thinkpad_wireless.dts`，与原理图 v2 对齐）：
   * `row-gpios` SENSE0：`P0.26` → `P1.05`（`<&gpio1 5>`）。
   * `col-gpios` DRV4：`P1.01` → `P0.26`（`<&gpio0 26>`，高驱动配置保留）。
   * `col-gpios` DRV13：`P1.05` → `P1.09`（`<&gpio1 9>`，高驱动配置保留）。
   * `mouse_ps2` 的 `rst-gpios`：`P1.09` → `P1.01`（`<&gpio1 1>`）。
4. **文档与注释修正**：全部 10 处代码注释（`.dts`/`.c`）及开发日志 v1.0.31/v1.0.32 条目中的 `BMD-341` 更正为 `BMD-340`，并补充天线差异说明。
5. **最终引脚状态**：16 个列驱动（DRV0~DRV15）全部落在标准驱动引脚；PS/2 CLK/DATA 在标准引脚（P0.06/P0.11）、RESET 在受限引脚（P1.01，静态合规）；CHG_INT 在 P1.03（受限，静态输入合规）；矩阵行输入与静态 LED 均按低频/静态用途分布于受限引脚，符合 BMD-34x 电气规范。
6. **Git**：`bmd340-module` 分支已推送 GitHub（`origin/bmd340-module`，当前 HEAD `d3972d5`），GitHub Actions 自动触发 CI 编译验证。

### [2026-08-13] v1.0.34 — 修复 CI 构建失败（GPIO_DS_ALT_LOW/HIGH 宏在 Zephyr 4.1 不可用）
1. **问题现象**：GitHub Actions Build #74~#77 连续失败（`thinkpad_wireless.dts:97: devicetree error: parse error: expected number or parenthesized expression`）。#73 及之前成功。
2. **根因分析**：失败始于 374e4be（"enable nRF52840 14mA High Drive (H0H1) for all DRV col-gpios"），该提交在全部 16 个 `col-gpios` 中加入了 `GPIO_DS_ALT_LOW | GPIO_DS_ALT_HIGH` 驱动强度标志。核查 Zephyr v4.1.0（ZMK 4.4.1 所用版本）的 `include/zephyr/dt-bindings/gpio/gpio.h` 与 `include/zephyr/drivers/gpio.h`：**均不存在 GPIO_DS_* 宏**（驱动强度标志是 Zephyr 4.2+ 才引入的特性）。devicetree 解析器将未展开的宏视为非法表达式，导致构建在 DTS 解析阶段失败。与 BMD-340 模块改动无关（#74 即已失败，早于模块分支）。
3. **修复方案**：移除全部 16 个 `col-gpios` 中的 `| GPIO_DS_ALT_LOW | GPIO_DS_ALT_HIGH`，恢复为 `GPIO_ACTIVE_LOW`（#73 及之前的可用状态）。nRF52840 默认标准驱动（约 0.5mA 灌电流保证值）足以覆盖同列 1~2 键同按（内部上拉 13kΩ，单键约 0.25mA）；BMD-340 模块的 16 个列驱动均已映射至标准驱动引脚（v1.0.33），模块级驱动能力有保证。
4. **后续选项**：若仍需 14mA 高驱动档，需等待 ZMK 升级 Zephyr ≥4.2（DTS `GPIO_DS_ALT_LOW/HIGH` 可用），或在 `board.c` 中直接操作 `NRF_P0->PIN_CNF[n].DRIVE` 寄存器（注意 ZMK kscan 运行时会重新 `gpio_pin_configure` 覆盖，需在 kscan 初始化完成后再设置，复杂度较高，暂不采用）。
5. **验证**：修复提交推送后触发 CI，Build #78 预期通过（devicetree 解析错误消除）。

### [2026-08-13] v1.0.35 — 依据 code review-2026-08-13 修复全部确定性缺陷
1. **背景**：对 `bmd340-module` 分支（HEAD `3db7c0c`）进行了三轮交叉审查（对照 BMD-340 数据手册 UBX-19033353 与网表 `BMD 340SCH.tel`），整合为 `Docs/review-2026-08-13.md`，本条目修复其中全部确定性 Bug 与工程问题，并通过本地 west + Zephyr SDK 编译验证。
2. **第一批（确定性 Bug）**：
   - **1.3 采样率越界读**：`zmk_mouse_ps2_set_sampling_rate` 将 `sizeof(allowed_sampling_rates)`（28 字节）当作元素个数，越界访问数组；改用 `ARRAY_SIZE()`，数组声明为 `static const int[]`。
   - **1.4 按键 sync 标志**：`zmk_mouse_ps2_activity_click_buttons` 中 `buttons_need_reporting--` 无条件执行，导致只有右键/中键变化时 sync 恒为 false，事件滞留缓冲区直到移动才送出；改为仅在触发的分支内递减。
   - **1.2 UART 写互斥锁多次释放**：`ps2_uart.c` 同一把 `ps2_uart_write_mutex` 被解锁 3 次（write_byte 末尾 / write_byte_start / write_finish，后者在 ISR/工作队列上下文），并发写保护失效；对齐 `ps2_gpio.c`，锁/解锁仅在 `ps2_uart_write_byte()` 内各一次。
   - **1.7 滚轮符号扩展**：`packet.scroll = packet_extra - ((packet.scroll << 3) & 0x100)` 中 `(packet.scroll<<3)&0x100` 恒 0，负向滚轮 0x08~0x0F 被解码为 +8~+15；改为 `(int8_t)(packet_extra << 4) >> 4` 做 4-bit 带符号扩展，并删除三行无意义 SET_BIT 死代码。
   - **2.2 missed-interrupt 缺 return**：`ps2_gpio_read_interrupt_handler` 超时 abort 后未 return，会把失效边沿误当新帧 start bit；补 `return`。
   - **2.3 dt_flags 复制粘贴错误**：`ps2_gpio_init_gpio` 第二行误写 `scl_gpio.dt_flags = 0`，改为 `sda_gpio.dt_flags = 0`。
   - **2.7 错误码恒打印 0**：`zmk_mouse_ps2_send_cmd` 失败日志用局部 `err`（恒 0），改用 `resp.err`。
3. **第二批（配置/工程）**：
   - **1.5 Kconfig 补齐**：`module/Kconfig` 新增 `PS2_LOG_LEVEL`、`ZMK_INPUT_MOUSE_PS2_ENABLE_ERROR_MITIGATION`、`PS2_GPIO/UART_ENABLE_PS2_RESEND_CALLBACK`、`ZMK_INPUT_MOUSE_PS2_ENABLE_PS2_RESEND_CALLBACK`、`PS2_GPIO_INTERRUPT_LOG_ENABLED`；此前这些被驱动代码引用的选项未定义，误码抑制/重发回调等功能被静默关闭。
   - **1.6 west.yml 锁定版本**：`revision: main` → 固定提交 `6e2ef41e022d555b10f116e395832913f71717b3`（2026-08-10 main HEAD，本项目验证基线；ZMK 官方 release tag 为 v0.1.0~v0.3.0），构建可复现，升级走显式提交。
   - **2.1 回调单字节缓冲竞争**：`ps2_gpio.c` / `ps2_uart.c` 的 `callback_byte` 单槽改为 32 槽 `K_MSGQ` FIFO；ISR 侧 `k_msgq_put(K_NO_WAIT)`，worker 侧循环排空 + 排空后复查再提交，消除"丢字节→重发"循环。
   - **2.5 回调队列 K_FOREVER 阻塞**：按键上报 `input_report_key(..., K_FOREVER)` 改为 `K_NO_WAIT`（与移动上报一致）。
   - **2.10 scale-divisor 除零/溢出**：`input_listener_ps2.c` 加 `scale_divisor == 0` 守卫，中间计算改 `int32_t`。
   - **2.11 LED 线程栈**：`status_leds.c` 栈 512 → 1024，defconfig 开启 `CONFIG_THREAD_STACK_INFO` 供实测。
   - **2.15 TrackPoint 参数调节行为未编译**：板级 DTS 新增 `zmk,behavior-mouse-setting` 节点实例（`&mms` 行为此前因无 devicetree 节点导致 `dt_compat_enabled` 为假、驱动完全不编译）。
   - **2.18 低电量关机未设 GPREGRET**：低电量 `sys_poweroff()` 前置位 `MANUAL_POWER_OFF_FLAG`，防包里误触反复"唤醒→低电→关机"放电。
4. **第三批（工程整理）**：
   - **1.8 README 引脚表**：按当前 DTS + 网表重写全部 16 列/8 行/PS2/指示灯引脚（BMD-340 模块引脚 U8.x），并注明"改 DTS 必须同步 README"。
   - **2.4 LED 定义重复 / caps lock 双所有权**：删除 DTS 中从未被引用的 5 个 LED 节点（bt/bat_r/bat_g/mute/mic_mute，实际由 status_leds.c 裸 GPIO 驱动）；caps lock P0.31 交还 `zmk,indicator-leds` 独占，board.c 开机灯序与 status_leds.c 关机灯序不再操作 P0.31；新建 `board_hw.h` 统一引脚宏与 `MANUAL_POWER_OFF_FLAG`（原两处重复定义）。
   - **2.6 lint.yml 反模式**：移除 clang-format 自动回写 + auto-commit（fork PR 会因 token 只读失败），改为 `--dry-run --Werror` 检查即报错；仓库根新增 `.clang-format`（LLVM + 2 空格 + K&R 大括号，匹配现有风格），并已用 clang-format-15 规范化全部模块源码。
   - **2.8 死代码**：删除 `ps2_uart.c` 的 `ps2_uart_write_byte_debug()`（100ms 忙等 bit-bang，恒返回 -1）与 `log_binary()`；`input_listener_ps2.c` 删除空函数 `handle_abs_code()` 及 UROB 死条件 `#if/#else`（两分支完全相同）。
   - **2.9 transform 重复矩阵坐标**：Unused 行 8 组重复 `RC()` 改为唯一坐标（含 `RC(5,15)` 行3/行5 重复），map 长度保持 130，消除 Studio 幽灵键。
   - **2.12 CI 产物与 CDC 冲突**：`build.yml` `create-full-image` 增加产物存在性检查（上游 artifact 命名变化不再静默失败）；`build.yaml` 移除 `zmk-usb-logging`（与 `studio-rpc-usb-uart` 共用 CDC ACM 口会帧交错），保留 Studio USB。
   - **2.16/2.17 PWRSWITCH 键值**：默认层 `&kp C_PWR` → `&none`（电源键只做本机开关机，不向主机发 HID 电源键）；bt 层 `&bt BT_CLR` → `&bt BT_SEL 0`（不再破坏性清除配对，未配对槽位自动广播），README 同步描述。
   - **3.2/3.4 杂项**：`gpio-ps2.yaml` 的"I2C bus"描述改为 PS/2；`zmk,input-listener.yaml` 重命名为 `zmk,input-listener-ps2.yaml`；`zmk,input-mouse-ps2.yaml` 删除驱动未读取的误导性 `layer-toggle` 属性；`settings_log` 拼写 `tp-tp-press-to-select-threshold` 修正；`init_thread` 的 `int` 强转指针改为直接传 `const struct device *`；`BOOT_DISPLAY_TICKS` 注释 5s→4.8s；`behavior_mouse_setting.c` 多余分号；`thinkpad_wireless.conf` 增加 `CONFIG_ZMK_BATTERY_REPORT_INTERVAL=10`（需求 10 秒检测，默认 60s 会滞后低电关机）；`merge_hex.py` `app_size` 按地址跨度 `max-min+1` 计算。
5. **矩阵驱动方式（review 1.1）**：按决策记录**保留推挽 + 中断驱动**设定，待 PCBA 实机验证；若出现 v1.0.29 记载的倒灌现象（左侧键区无响应 / 同列 ≥2 键异常），再切换 `GPIO_OPEN_DRAIN`（可配 `GPIO_PULL_UP`）。
6. **验证**：本地 `west` + Zephyr SDK 0.17.0（Zephyr 4.1.0+zmk-fixes）对 `thinkpad_wireless` 板完整编译通过；`clang-format-15 --dry-run --Werror` 全模块源码零违规；transform map 130 项无重复坐标。
### [2026-08-16] v1.0.36 — CoB 分支完成与 bmd340-module 同步，恢复 USB 日志串口
1. **分支同步（0816970）**：将 `bmd340-module` 分支的工程改进整体合入 CoB（直贴芯片）分支，同时保持 CoB 硬件设计：仓库根新增 `.clang-format`（LLVM + 2 空格）、`lint.yml` 改为 `--dry-run --Werror`（移除自动回写 + auto-commit 反模式）、`build.yml` `create-full-image` 增加产物存在性检查、`west.yml` 锁定 ZMK 到固定提交 `6e2ef41e`、同步 `Docs/review-2026-08-13.md` 与开发日志（v1.0.34/35）、删除遗留 `Remain.md`。
2. **恢复 USB 调试串口（32d7b50）**：`build.yaml` 恢复 `zmk-usb-logging` snippet。review 2026-08-13 item 2.12 曾因与 `studio-rpc-usb-uart` 共用同一 CDC ACM 口（帧交错）将其移除；2026-08-16 按调试需求恢复两者并存。若 Studio 二进制帧乱码，本地临时改为二选一。

### [2026-08-16] v1.0.37 — PS/2 总线修复（上拉 + 开漏输出）、NVS 设置后端、Studio 解锁组合键
1. **SCL/SDA 输入保留上拉（df08cf0 P1）**：PS/2 写抑制释放后时钟线失去上拉，主机写命令在 pos=1 处超时；`ps2_gpio` 输入配置恢复 `GPIO_PULL_UP`。
2. **NVS 设置后端（df08cf0 P2）**：`thinkpad_wireless_defconfig` 开启 `CONFIG_FLASH`/`CONFIG_FLASH_MAP`/`CONFIG_NVS`/`CONFIG_SETTINGS_NVS`，`chosen` 注册 `zephyr,settings = &storage_partition`。修复蓝牙 `bt_gatt Database Hash err -2`、`Unable to store name`、配对信息不持久等问题；此前该修复仅在 bmd340-module（9baaec7）验证，本次对齐应用到 CoB 直贴芯片设计。
3. **Studio 解锁组合键（df08cf0 P4）**：Q+P+J 组合（positions 33/42/55）绑定 `&studio_unlock`，与 bmd340 分支行为一致。
4. **SCL/SDA 开漏输出（4ffc6e7）**：PS/2 主机侧不得主动把 CLK 拉高——推挽高电平会与器件开漏低电平"打架"，主机写永远无法开始；输出模式改 `GPIO_OPEN_DRAIN`，由外部上拉维持释放态高电平。

### [2026-08-16] v1.0.38 — 日志级别与 Studio 解锁窗口调整
1. **日志降级（40300fc）**：`CONFIG_ZMK_LOG_LEVEL_WRN=y` 抑制 `zmk_usb_get_conn_state` 每 ~80ms 刷屏；PS/2 驱动保留自有 `CONFIG_PS2_LOG_LEVEL`（module/Kconfig，INFO）供 bring-up 调试。
2. **解锁窗口（40300fc）**：Q+P+J 解锁组合 `timeout-ms` 50 → 150 ms，放宽三角按键窗口，降低实机误触发困难。
3. **保留 DEBUG + 本地 usb.c 补丁（491dd5b）**：调试期恢复 `CONFIG_ZMK_LOG_LEVEL_DEBUG`，改为对本地 `zmk/app/src/usb.c` 打补丁——`zmk_usb_get_conn_state()` 仅在 USB 状态变化时打印（静态缓存上次状态），保留全量日志的同时消除刷屏。注意：该补丁位于本地 vendored ZMK 工作区（`gitignore`，不随仓库 CI 生效），CI 构建仍为 DEBUG 全量日志，属调试期已知取舍。

### [2026-08-17] v1.0.39 — X220 矩阵 bring-up 修正（SENSE6/7 交换、重复 transform 清理、INS/K_CMENU 键位、静音 LED 本地翻转）
1. **SENSE6/SENSE7 行交换（68a019c）**：实机验证物理底行与 thinkpad-ec X220 表相反——Z X C V M , . Enter RShift RCtrl 实际在 SENSE7，B N / Space Down RAlt 在 SENSE6；`thinkpad_wireless.dts` 两行 `row-gpios` 对调。
2. **重复 transform 坐标清理（68a019c）**：删除 8 个重复坐标（review 2.9 遗留），此前因 last-wins 查找劫持了 PSCRN/SLCK/LCTRL 三个键。
3. **键位补齐（68a019c）**：绑定 INS（pos 112，matrix (0,9)，bring-up 验证）与 K_CMENU（pos 46，X220 菜单键，matrix (4,11)）。
4. **静音 LED 本地翻转（68a019c）**：mute / mic-mute 两枚 LED 改由 `status_leds.c` 本地按键监听翻转（标准 HID LED 报表无静音位）；caps lock 保持 `zmk,indicator-leds` 主机同步。

### [2026-08-17] v1.0.40 — mic-mute 键改发 HID Consumer 0xE9
1. **现象（c709cec）**：pos 99（matrix (6,10)，板上有独立 `-LED_MUTE` / `-LEDMICMUTE_R` 信号）与扬声器静音共用 `&kp C_MUTE`，Windows 11 无法区分。
2. **修复**：Windows 11 麦克风静音使用 HID Consumer usage 0xE9（扬声器静音为 0xE2），ZMK 无 `C_MIC_MUTE` 键码，在 keymap 本地定义 `#define C_MIC_MUTE (ZMK_HID_USAGE(HID_USAGE_CONSUMER, 0xE9))` 并绑定该键；LED 仍由 status_leds.c 本地翻转。

### [2026-08-17] v1.0.41 — ThinkVantage BT 层：2 秒长按配对 + 电源灯快闪
1. **ThinkVantage 键改层（329dc9c）**：pos 100（matrix (5,10)）由 `&kp F13` 改为 `mo 1` 激活 BT 层——ThinkVantage+1..5（bt 层第 2 行）切换 BT 设备；Fn 保留 `mo 1`。
2. **新增 `ht_bt_pair` hold-tap（329dc9c）**：`flavor = "hold-preferred"`、`tapping-term-ms = <2000>`。ThinkVantage/Fn + Power 按住 ≥2s 触发 `BT_SEL 0`（配对/广播），短按无动作（防误配对）；bt 层 Row 8 PWRSWITCH 位置绑定该行为。
3. **电源灯配对快闪（329dc9c）**：`status_leds.c` 新增监听——BT 层 1 激活（`zmk_layer_state_changed` layer==1）且 Power（position 129 = PWRSWITCH）按住 ≥2s 时，电源 LED 以 ~12.5Hz 快闪代替呼吸（镜像 hold-tap 时序），呼吸相位冻结，松开恢复。

### [2026-08-17] v1.0.42 — 修复 Build #93 编译失败（hold-tap bindings 只能裸 phandle）
1. **问题现象**：Build #93（GitHub Actions）失败于 DTS 解析阶段：`DTError: expected property 'bindings' on /behaviors/ht_bt_pair ... not 'bindings = < &none >, < &bt 0x3 0x0 >;'`，C 代码尚未开始编译。
2. **根因分析**：v1.0.41 的 `ht_bt_pair` 在 `bindings` 属性中写了 `<&bt BT_SEL 0>`（带 2 个 cell 参数），但 `zmk,behavior-hold-tap` 绑定的 `bindings` 是 `type: phandles`（只允许裸 phandle，不允许附加参数），gen_edt 直接报错。
3. **修复方案（849ced0）**：`bindings` 改为裸 phandle `bindings = <&bt>, <&none>;`，参数改经键位用法单元格传递——usage `&ht_bt_pair BT_SEL 0` 中第 1 个 cell 由驱动转发为 hold 绑定的 `param1`（= `BT_SEL_CMD`），第 2 个 cell 转发为 tap 绑定 `param1`（`&none` 忽略）；`&bt` 的 `param2` 缺省为 0，即选中 profile 0。语义与 v1.0.41 意图一致（hold ≥2s → `zmk_ble_prof_select(0)` 配对/广播，短按无动作）。同提交修复 `status_leds.c` 文件级声明缩进错乱（列 0，符合 `.clang-format`）。
4. **验证**：本地按 CI 命令复刻（`west build -b thinkpad_wireless -S "studio-rpc-usb-uart zmk-usb-logging" -- -DZMK_CONFIG=... -DZMK_EXTRA_MODULES=...`，Zephyr SDK 0.17.0）完整编译通过：FLASH 321336 B（39.62%）、RAM 87098 B（33.23%），`zmk.hex` 正常产出；`status_leds.c` 新增监听（`zmk_layer_state_changed` / position 129）API 与固定 ZMK 版本 `6e2ef41e` 匹配。4. **验证**：本地按 CI 命令复刻（`west build -b thinkpad_wireless -S "studio-rpc-usb-uart zmk-usb-logging" -- -DZMK_CONFIG=... -DZMK_EXTRA_MODULES=...`，Zephyr SDK 0.17.0）完整编译通过：FLASH 321336 B（39.62%）、RAM 87098 B（33.23%），`zmk.hex` 正常产出；`status_leds.c` 新增监听（`zmk_layer_state_changed` / position 129）API 与固定 ZMK 版本 `6e2ef41e` 匹配。

### [2026-08-17] v1.0.43 — MicMute 误触音量键修复；配对电源灯严格镜像 hold-tap 窗口
1. **现象（实机日志）**：
   - pos 99（MicMute）按下后日志显示 `usage_page 0x0C keycode 0xE9`，Windows 表现为**音量+**。
   - ThinkVantage+Power 按住 ≥2s 后电源灯无快闪；日志分析确认**从未进入配对流程**。
2. **根因分析**：
   - **MicMute**：HID Consumer 页 0xE9 = AC Volume Increment（音量+）。v1.0.40（c709cec）假设"Windows 11 麦克风静音用 0xE9"是错误判断。规范中正确的系统级码是 **Generic Desktop 页 0xA9（System Microphone Mute）**，但 a) ZMK 的 `zmk_hid_press()` 仅接受 KEY(0x07)/CONSUMER(0x0C) 两个 usage page，GD 页用法直接返回 -EINVAL（上游 issue #1535 为开放功能请求），b) Windows 11 亦无内置处理 GD 0xA9（微软官方论坛 HUTRR110 问答：开发者 USBPcap 实测无反应，微软员工确认无内置支持）。ZMK 官方键码表无任何麦克风静音键码。
   - **配对未触发**：日志显示每次 Power（pos 129）按下时 BT 层 1 均为关闭状态（11.455/13.979/21.724 按下前层分别于 11.203/13.954/21.713 关闭），pos 129 每次都以 `binding name: none` 解析——ZMK **在按下瞬间解析绑定**，之后层激活不会重解析已按住的键（日志实证：层激活期间 pos 129 无重发事件），因此 Power 先按、ThinkVantage 后按的组合**永远无法**到达 ht_bt_pair；且日志中 Power 最常按住仅 1.45s（< 2s），原 LED 判定（按物理按下时刻计时）也正确未亮。附带观察：pos 100（ThinkVantage）短时间内多次快速通断（14.227~15.314 间 4 次），疑似触点抖动或操作未稳。
3. **修复方案（36c5c88）**：
   - keymap：`C_MIC_MUTE` 由 Consumer 0xE9 改为 **Consumer 0xD5（Start or Stop Microphone Capture）**——语义正确的消费类码、ZMK 可发送、不再误触音量+；注释记录完整溯源（0xE9=音量+ / GD 0xA9 规范正确但 ZMK 发不了且 Windows 不认 / 0xD5 为可行折衷，Teams 可另配 Ctrl+Shift+M 宏）。Row 8 注释明确手势顺序：**先按 ThinkVantage/Fn 激活层 1，再按 Power ≥2s**。
   - `status_leds.c`：配对 2s 计时仅在 **Power 于层 1 激活状态按下时**开始（`pwr_key_press_time = layer1_active ? now : 0`），松开或层 1 关闭即失效（=0 哨兵，配对判定额外要求 `pwr_key_press_time != 0`），精确镜像 ht_bt_pair 的绑定窗口，杜绝"层外按下→假配对闪烁"。
4. **验证**：本地按 CI 命令复刻完整编译通过：FLASH 321384 B（39.63%）、RAM 87098 B（33.23%），`zmk.hex` 正常产出。实机操作顺序应为：按住 ThinkVantage（或 Fn）→ 再按住 Power ≥2s → 电源灯快闪进入配对。4. **验证**：本地按 CI 命令复刻完整编译通过：FLASH 321384 B（39.63%）、RAM 87098 B（33.23%），`zmk.hex` 正常产出。实机操作顺序应为：按住 ThinkVantage（或 Fn）→ 再按住 Power ≥2s → 电源灯快闪进入配对。

### [2026-08-17] v1.0.44 — VBUS 检测改事件驱动（删除轮询）；压低 PS/2 点击级日志
1. **背景**：上电日志持续刷 `zmk_usb_get_conn_state: state: 3`（每 ~80ms 一条）——`status_leds.c` 低电量关机与充电指示灯在 LED 线程**每个 tick** 调用 `zmk_usb_is_powered()`，而该调用在未打补丁的 ZMK 中每次执行 `LOG_DBG`。先按降频思路处理（1s、2min 轮询），随后复核硬件链路发现问题根源：**VBUS 检测本应是中断事件，无需轮询**。
2. **硬件实证（zephyr usb_dc_nrfx.c）**：nRF52840 POWER 外设自带 USB 检测中断——`usb_dc_power_event_handler()` 处理 `NRFX_POWER_USB_EVT_DETECTED/READY/REMOVED`（VBUS 上电/稳定/移除），映射到 `USB_DC_CONNECTED/DISCONNECTED` 状态回调；ZMK `usb.c` 在状态变化时 raise `zmk_usb_conn_state_changed` 事件。插充电器（不枚举）同样触发（POWERED 状态独立于枚举流程），拔线触发 REMOVED。**不存在"必须轮询"的硬件场景**。
3. **修复方案（619edee）**：
   - `status_leds.c`：删除 VBUS 轮询（含 80ms→1s→2min 的各级节流），改为订阅 `zmk_usb_conn_state_changed` 事件更新 `vbus_present`（`conn_state != ZMK_USB_CONN_NONE`），LED 线程仅在启动时读一次初值。充电指示灯/低电量关机判定零轮询、插拔即时响应，附带消除刷屏（不改 ZMK 日志级别也能彻底安静）。
   - 顺带压低两处"每次操作必打"的 INFO 日志：`input_mouse_ps2.c` 指点杆六路按键日志（按下/释放 × 左中右）降为 DBG；`ps2_uart.c` 写中断处理中每 SCL bit 一条的 `Inside ps2_uart_write_scl_interrupt_handler_blocking` 降为 DBG（当前用 gpio 模式未触发，属隐患）。
   - 保留说明：`CONFIG_ZMK_BATTERY_REPORT_INTERVAL=10`（电量采样 10s）不随本次改动——低电量关机（<2% SoC 且无 VBUS）的响应依赖它及时更新，采样间隔拉太长会滞后关机保护（v1.0.35）。
4. **验证**：本地按 CI 命令复刻完整编译通过：FLASH 321700 B（39.67%）、RAM 87098 B（33.23%）。上电后不再出现 80ms 刷屏；插拔 USB（含纯充电口）由中断事件即时更新充电指示灯。
### [2026-08-17] v1.0.45 — PS/2 指点杆 bring-up：RST 极性实证修正（高电平复位，只拉低不脉冲）+ 完整镜像应用标记修复
1. **RST 极性实证（实机日志交叉对比）**：
   - 原始固件假设"低电平复位"：P1.09 拉低 600ms 后释放回高。实测发现：**模块仅在 P1.09 为低时通信，拉高即永久静默**——旧固件 1.967s 释放回高后，设备再未响应任何命令。
   - 模块上电后（~1.6s）自行执行 POR 并发送 BAT 结果，与 RST 电平无关；板上 R14（5.1K）把 RST 上拉，模块自始至终被按在复位态，直到固件显式拉低。
2. **启动窗口关键发现**：RST 电平在模块启动窗口（释放后 ~300ms 的 POST 期）决定其命运——**RST 低 → 正常发送一次 BAT 结果后安静等待主机；RST 高 → 卡入持续吐乱码状态**（0xFF 类帧、偶校验有效：0xfc/0x7e/0xf3/0xd7/0xf5…），释放后也不恢复。
3. **修复方案**：
   - `module/drivers/input_mouse_ps2.c`：`zmk_mouse_ps2_init_power_on_reset()` 删除 600ms 复位脉冲，改为**直接输出低并保持**（模块上电即被板上上拉复位，此即足够复位期）；注释记录完整实证。
   - 新增引脚回读日志：`RST pin P1.09 driven low: raw=0 PIN_CNF=0x...`（raw=0 + DIR=output 即证明固件真正驱动为低，便于排查飞线/接线问题）。
   - `thinkpad_wireless.dts`：`rst-gpios` 由 `GPIO_ACTIVE_LOW` 改为 `GPIO_ACTIVE_HIGH`（高电平复位/低电平运行）。
   - `module/drivers/ps2_gpio.c`：启动版本标记升级为 **v5**（`PS/2 config v5: ... RST held LOW (no pulse)`），旧固件（600ms 脉冲 / RST 回高）在 log 中一眼可辨。
4. **完整镜像合并修复**：`tools/merge_hex.py` 合并完整镜像**必须加 `--mark-app-valid`**（在 0xFF000 settings 页写入 bank_0=0x0001 + crc=0 + app_size）。缺失时 adafruit bootloader 判定应用无效、停留在 DFU 模式，设备不枚举串口/HID（无响应）。烧录完整镜像需勾选 "Erase all"。`firmware/` 目录按 `thinkpad_wireless_full_v5.hex` 命名输出带版本完整镜像。
5. **验证**：本地按 CI 命令复刻完整编译通过：FLASH 324100 B（39.96%）、RAM 167170 B（63.77%）。v5 固件实机 log 确认：`raw=0`、无 600ms 脉冲、RST 保持低。
6. **当前 bring-up 状态（未解决）**：RST 问题已闭环，但设备传输仍为乱码（读帧 `0xfd/0xff/0xc0/0xfc/0xf9`，无 BAT 成功码 0xAA），主机写从未成功（全部 scl timeout，设备不响应）。中断日志显示读采样点 scl 全为 0（上升沿触发但采样时已回落）且数据位呈 1/0 交替——CLK 波形异常（高电平极短/振荡）。证据指向**模块供电与信号通路**：TP4 规格要求 VCC > 4.5V（当前调试用 3.3V，欠压运行），且 CLK/DATA 上拉轨 VDD3V3/5V_CONN（FPC2.19）在 CoB 板上无供电来源。下一步：模组供电改 5V、示波器验证 CLK/DATA 波形、模组接已知良好 PS/2 主机验证。
### [2026-08-21] v1.0.46 — bmd340-module 实机启动阻塞修复：LFCLK 改内部 RC（boot 卡死闭环）+ BMD340 固件归档命名
1. **现象**：烧录完整镜像（bootloader 标记正确、UICR=0xF4000、MBR 完好）后，上电既不枚举 USB（无 HID/串口）也不广播 BLE；J-Link 正常连接后 `memrd` 全部可读但伴随 `-256` 噪音（SWO/SWCLK 接反导致，正确接线后消失）。
2. **排查链（J-Link 寄存器实证）**：
   - `GPREGRET(0x4000051C)=0x0000`：排除电源管理开机门（8s 关机/低电量关机 0xAA 锁）。提醒：nRF52840 的 GPREGRET 偏移是 **0x51C**，0x540 是 GPIOREGRET。
   - `UICR(0x10001014)=0x000F4000`：bootloader 引导链目标完好；`RESETREAS=0`：无复位风暴。
   - `LFCLKSTAT(0x418)=0x00010000` → 按 SVD 位定义（bits0-1=SRC、bit16=STATE）实际为 **SRC=RC + running=1**——**nRF52 硬件在 XTAL 缺位时自动回退到内部 RC（用户判断正确）**；`HFCLKSTAT(0x40C)=0x00010000` 同为 **SRC=HFINT + running=1**。
   - `EVENTS_HFCLKSTARTED/LFCLKSTARTED(0x100/0x104)=0`：**回退运行不置位 STARTED 事件**——而 Zephyr 时钟驱动按"目标类型匹配"轮询等待，LFCLK 请求为严格 XTAL 时永不满足 → 应用在时钟启动处死等（PC/LR 落于 `nrf_clock_event_clear`/`__set_BASEPRI_MAX` 附近、PRIMASK=1）。
3. **根因**：板级原理图有外置 32.768kHz X1（FC-135R），**当前硬件未焊**；固件未配置 LFCLK 源，Zephyr nRF 默认 `CLOCK_CONTROL_NRF_K32SRC_XTAL` → 死等。32MHz 由 BMD-340 模块内部提供（bootloader 状态下实测 `HFCLKSTAT.SRC=XTAL + running=1`、两个 STARTED 事件均触发，模块晶振完好）。
4. **修复**：`config/thinkpad_wireless.conf` 增加 `CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y`，LFCLK 显式走内部 RC，不再依赖 32.768k。焊上 X1 后可删除该配置恢复 XTAL。注意：**USB 与 BLE 仍依赖 32MHz（模块内置）**，本修复不改变该依赖。
5. **固件归档命名约定**：`firmware/thinkpad_wireless_BMD340_full_v{版本}.hex`（全片镜像）与 `..._app_v{版本}.uf2`（拖拽用）以 BMD340 标识区分 nRF52840_CoB 的 `thinkpad_wireless_full_v{版本}.hex`；新增 `tools/make_uf2.py`（nRF52 绝对地址 UF2 转换，family 0xADA52840）。
6. **验证**：v1.0.46 烧录后 bootloader 直接弹 nice!nano U 盘（烧录流程复位触发的 DFU 入口，非应用无效）；干净单次上电后 **HID 键盘 + MOUSE（小红帽）均出现**，boot 链路闭环。FLASH 30.64% / RAM 56.47%（无 snippets 的纯 HID 构建）。
### [2026-08-21] v1.0.47 — USB 调试串口修复：本地构建需以 `west -S` 显式应用 build.yaml snippets
1. **现象**：v1.0.46 已能出 HID/MOUSE，但设备管理器无 CDC 串口。
2. **根因**：`build.yaml` 的 `snippet: studio-rpc-usb-uart zmk-usb-logging` 只被 CI 的 `build-user-config.yml` 解析（经 `west -S` 传入）；**本地直接 `west build` 不读取 build.yaml**，固件未含 CDC-ACM 串口设备 → 只有 ZMK 核心 HID 接口。
3. **修复**：按 CI 等价方式重建：`west build ... -S "studio-rpc-usb-uart zmk-usb-logging"`。验证 `zephyr,console`/`zephyr,shell-uart`/`zmk,studio-rpc-uart` 三个 chosen 全部指向 CDC-ACM 节点、`CONFIG_ZMK_USB_LOGGING=y`。
4. **产物重归档为 v1.0.47**：`thinkpad_wireless_BMD340_full_v1.0.47.hex`（app SP=0x200261E8 / RESET=0x35141 / size=0x4F878，FLASH 40.17% / RAM 63.80%，settings bank_0=0x0001）。
5. **当前状态**：HID 键盘 + MOUSE + 调试串口三通道就绪；PS/2 乱码问题（TP4 供电 3.3V 欠压/上拉轨无源）仍待模组侧 5V 供电验证。
### [2026-08-21] v0.2 — 首个稳定发布：修复 PrtSc 键与 Fn 键（两处实机回归）
1. **修复：PrtSc 有 log 但 Windows 无响应**。根因：68a019c（v1.0.39 去重）将 transform
   Row 6 pos 105（PSCRN）的坐标从 X220 官方表 PrtSc 坐标（drive13/sense1）误改为
   矩阵空位（X220 表 0x00），而物理 PrtSc 实际落在该官方坐标上 → 按键被解析到
   pos 93（`&trans`），固件不发任何 HID。修复：pos 105 恢复官方坐标，幽灵空位移入
   pos 93（仍为 `&trans`），transform 保持 130 项无重复（脚本校验通过）。
2. **修复：Fn 键完全无事件（无 log、无层切换）**。根因：v23（11b1270）在
   `status_leds_init()` 中对 P1.08（HOTKEY/Fn）与 P1.11（PWRSWITCH）裸调用
   `gpio_pin_configure(GPIO_INPUT|GPIO_PULL_UP)`；Zephyr 4.1 gpio_nrfx 在引脚被
   重新配置时会**删除已配置的 GPIOTE 触发**（“Remove previously configured trigger
   when pin is reconfigured”），而 direct kscan 的中断在 physical_layouts_init 中先
   行挂载（同优先级、链接顺序在其后）——开机后 Fn/电源键的扫描中断永久失效。
   修复：删除这两行重配置（引脚由 kscan 按 DTS 配置输入+上拉；配对/8s 关机等
   `gpio_pin_get_raw` 裸读不依赖配置者，不受影响）。修复后 Fn 恢复 `mo 1`
   （Fn+1..5 切换 BT），电源键 kscan 事件同步恢复。
3. **验证**：本地按 CI 等价命令（`west build -b thinkpad_wireless -S
   "studio-rpc-usb-uart zmk-usb-logging"` + ZMK_CONFIG/ZMK_EXTRA_MODULES）编译通过；
   transform 校验 130 项唯一。实机：键盘矩阵、Fn/BT 层、蓝牙配对、USB HID + CDC
   串口正常；PS/2（小红帽）与电池功能仍待调试。
4. **发布**：Github tag `v0.2`，产物 `firmware/thinkpad_wireless_BMD340_app_v0.2.uf2`
   与 `firmware/thinkpad_wireless_BMD340_full_v0.2.hex`（完整镜像含 bootloader +
   settings 有效标记）。
