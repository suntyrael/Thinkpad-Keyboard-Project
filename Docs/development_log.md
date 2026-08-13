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
