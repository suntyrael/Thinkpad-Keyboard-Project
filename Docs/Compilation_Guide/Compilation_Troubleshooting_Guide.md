# ZMK / Zephyr 固件编译故障排查与编码规范指南

本指南基于 ThinkPad 键盘无线化项目（ZMK Firmware）的首次编译成功经验，总结了在 Windows 物理开发环境与 Linux 自动化编译流水线之间遇到的核心兼容性问题、重构规范及外设依赖管理，以帮助后续开发者与 AI 助手预防编译失败。

---

## 1. 核心编译问题与底层解决方案 (Troubleshooting & Solutions)

### 1.1 文件编码污染 (UTF-8 BOM Header)
*   **报错现象**：Linux 构建环境（如 GitHub Actions）报错提示 unknown token at start of line，或 Kconfig、YAML 语法解析错误。
*   **根本原因**：Windows 部分文本编辑器在保存 .dts、Kconfig、.conf 或 .yaml 文件时，会在文件开头自动插入 UTF-8 BOM 签名字节（十六进制为 EF BB BF）。
*   **解决方案**：所有项目配置文件、设备树及源文件必须强制保存为 **UTF-8 无 BOM (without BOM)** 格式，换行符统一使用 **LF**。

### 1.2 ZMK HWMv2 目录嵌套规则
*   **报错现象**：构建系统提示找不到自定义板级配置，或提示 Kconfig 存在循环依赖冲突。
*   **根本原因**：ZMK 4.0 / Zephyr 4.1.0 引入了 Hardware Model v2 (HWMv2) 规范，舍弃了传统的 oards/arm/ 架构式排布，要求采用嵌套的厂商命名模式。
*   **解决方案**：
    1. 板级目录结构必须调整为 module/boards/<vendor>/<board_name>（如 module/boards/thinkpad/thinkpad_wireless）。
    2. Kconfig.board 文件必须重命名为 Kconfig.<board_name>（如 Kconfig.thinkpad_wireless）。
    3. 板级 CMakeLists.txt 引入上层核心头文件时的相对路径需要相应调整级数（如从 ../../include 改为 ../../../include）。

### 1.3 自定义 DTS 绑定检索失败
*   **报错现象**：编译底层驱动文件时，报 __device_dts_ord_... 未定义的 C 预处理宏错误。
*   **根本原因**：自定义的外设驱动所声明的 compatible 属性 YAML 绑定文件放置在自定义模块中，若未显式注册，Zephyr 的 DTS 编译器将无法发现这些绑定定义。
*   **解决方案**：在模块的 zephyr/module.yml 配置文件中显式配置 dts_root 属性：
    `yaml
    build:
      settings:
        board_root: module
        dts_root: module
    `

### 1.4 自定义编译模块的头文件包含隔离
*   **报错现象**：自定义模块内的驱动源文件（如 input_listener_ps2.c）在引用 ZMK 应用层头文件（如 <zmk/endpoints.h>）时，报找不到目录。
*   **根本原因**：在 CMake 构建体系中，自定义模块作为一个独立的静态库 target 构建，默认无法继承主应用的包含路径。
*   **解决方案**：在模块的 CMakeLists.txt 中添加 ZMK 应用包含路径的局部注入：
    `cmake
    zephyr_library_include_directories(/include)
    `

### 1.5 蓝牙名称与 GAP 名称的静态断言限制
*   **报错现象**：编译时触发 BUILD_ASSERT 静态断言错误。
*   **根本原因**：
    1. ZMK le.c 强制限制键盘蓝牙展示名 CONFIG_ZMK_KEYBOARD_NAME 长度不得超过 16 字节。
    2. Zephyr 主机蓝牙栈限制 GAP 广播名称 CONFIG_BT_DEVICE_NAME 的长度必须小于 CONFIG_BT_DEVICE_NAME_MAX（通常为 16）。
*   **解决方案**：在配置中将蓝牙显示名控制在 16 字节内（例如 "ThinkpadWireless"），将 GAP 广播名控制在 15 字节内（例如 "ThinkpadWL"）。

### 1.6 依赖物理控制器的默认关闭状态
*   **报错现象**：
    - 电量采样模块编译中断，提示 #error Unsupported ADC。
    - 指示灯或电源管理相关的 USB 接口在链接时报对 zmk_usb_get_conn_state 的 undefined reference 未定义引用。
*   **根本原因**：Nordic 芯片的基准设备树中，dc (SAADC) 和 usbd (USB控制器) 的物理状态默认设为了 "disabled"。如果未在板级 DTS 中显式启用，相关的外设驱动宏就不会生效，进而导致应用层代码编译或链接缺失。
*   **解决方案**：在板级设备树 .dts 尾部显式激活这些控制器，并在 _defconfig 中使能驱动：
    `dts
    &adc { status = "okay"; };
    &usbd { status = "okay"; };
    &gpiote { status = "okay"; };
    `

---

## 2. 自动化防错与工作区规则集成

为了固化上述经验，项目工作区中已经自动集成了以下配置，它们会在每次开发时自动指导 AI 助手并为开发者提供本地校验：

1.  **全局提示词规则 (Workspace Agent Rules)**：
    *   存放路径：[.agents/AGENTS.md](file:///e:/Work/个人文档/业余研究/Thinkpad keyboard wireless/.agents/AGENTS.md)
    *   作用：定义了文件编码、外设配置齐步走、蓝牙名称长度等核心开发约束，任何 AI 助手在进入工作区时均会自动遵循。
2.  **本地预检技能 (Custom Skill)**：
    *   存放路径：[.agents/skills/zmk-build-helper/SKILL.md](file:///e:/Work/个人文档/业余研究/Thinkpad keyboard wireless/.agents/skills/zmk-build-helper/SKILL.md)
    *   内容：汇总了详细的故障排查信息、外设依赖排查表，并内置了用于检测工作区 UTF-8 BOM 编码污染以及蓝牙名称长度的 PowerShell 验证脚本。