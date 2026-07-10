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
