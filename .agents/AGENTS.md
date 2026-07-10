# ZMK / Zephyr Firmware Project Rules

为了确保跨平台固件构建的稳定性、代码合规性以及与 ZMK 最新生态（如 ZMK Studio、HWMv2）的兼容性，进行代码修改或新外设/驱动适配时，必须严格遵守以下通用规范。

## 1. 法律合规与代码规范 (Legal & Coding Standards)

### 1.1 Clean Room 净室设计原则 (MIT 许可合规)
- ZMK 项目采用宽松的 MIT 许可证。任何贡献或自用代码**严禁**直接引用、抄袭或参考 GPL 协议项目（例如 QMK、TMK 等）的代码。
- 仅允许阅读其他 GPL 项目的文档以实现等效功能，在编写自定义驱动（如 PS/2 驱动、Trackpoint 功能）时必须完全自主编码。

### 1.2 代码格式化规范 (Formatting)
- 所有的 .c 和 .h 源文件必须在提交前使用 clang-format 进行格式化。
- 所有的说明文档 (.md) 必须使用 Prettier 格式化。

### 1.3 UTF-8 无 BOM (Critical)
- 所有源文件和配置文件（.c, .h, .dts, .yaml, Kconfig, defconfig, .yml, .conf 等）必须采用 **UTF-8 无 BOM** 编码，且换行符统一使用 **LF**。

---

## 2. 设备树与 ZMK 核心配置规范

### 2.1 物理布局 (Physical Layouts) 与 ZMK Studio 适配
- 键盘必须遵循 HWMv2 物理布局与矩阵分离的规范：
  - 物理布局应在独立的 <keyboard>-layouts.dtsi 中定义，且节点 compatible 属性为 "zmk,physical-layout"。
  - 在板级设备树（.dts）的 chosen 节点中，必须明确指定 zmk,physical-layout，例如：
    `dts
    chosen {
        zmk,physical-layout = &physical_layout0;
    };
    `

### 2.2 矩阵变换 (Matrix Transform) 与 KSCAN 驱动
- 任何物理引脚排列（KSCAN）与逻辑键位映射不一致时，必须通过 zmk,matrix-transform 进行映射，并在 chosen 节点中注册 zmk,matrix-transform = &default_transform;。

### 2.3 自定义行为 (Behaviors) 与组合键 (Combos)
- **自定义行为**：所有的 Hold-Tap、Tap-Dance、Mod-Morph、Macro 等自定义行为必须在设备树的 ehaviors 节点中定义，且指定正确的 compatible。Hold-Tap 类型必须显式指定 lavor、	apping-term-ms 并在 #binding-cells 中填入 <2>。
- **组合键**：Combos 必须在设备树根节点的 combos 容器中定义，并明确指定 key-positions。

---

## 3. 编译系统与头文件规范

### 3.1 Zephyr 头文件引入命名空间
- 引用 Zephyr 核心及外设头文件时，必须采用现代命名空间格式 <zephyr/...>。
  * **示例**：#include <zephyr/kernel.h>, #include <zephyr/drivers/gpio.h>
- 引用 ZMK 键值或蓝牙 API 时，在 DTS 文件中引入 <dt-bindings/zmk/...>，在 C 文件中引入 <zmk/...>。

### 3.2 驱动初始化宏与 API 签名匹配
- 宏声明与回调函数的签名必须与当前 Zephyr 内核版本相匹配（如 Zephyr 4.x 的 SYS_INIT 的签名应为 int init_fn(void)；INPUT_CALLBACK_DEFINE 应接收 3 个参数）。

### 3.3 自定义模块 CMake 隔离与相对路径
- 自定义静态模块在 CMakeLists.txt 中需要通过 zephyr_library_include_directories(/include) 来访问应用层头文件，不得使用宿主机绝对路径。

### 3.4 设备树绑定文件 (DTS Bindings) 与模块注册
- 自定义 compatible 节点必须提供配套 YAML 绑定，并在模块的 zephyr/module.yml 中注册 dts_root 搜索范围：
  `yaml
  build:
    settings:
      board_root: module
      dts_root: module
  `

---

## 4. 外设使能与硬件控制器使用规范

### 4.1 外设使能状态与驱动 Kconfig 齐步走
- 代码中使用任何外设控制器（GPIO、ADC、PWM、USB 等）时，必须同步确保：
  1. **设备树使能**：该外设物理节点在板级 .dts 中已被激活（status = "okay";）。
  2. **驱动使能**：在板级 _defconfig 中使能该外设的驱动宏（如 CONFIG_ADC_NRFX_SAADC=y, CONFIG_PWM=y, CONFIG_USB_DEVICE_STACK=y）。

---

## 5. 网络与通信限制限制

### 5.1 蓝牙名称与设备标识符长度限制
- **CONFIG_ZMK_KEYBOARD_NAME**（ZMK 蓝牙展示名称）：长度**不得超过 16 个字符**。
- **CONFIG_BT_DEVICE_NAME**（GAP 广播名称）：长度必须**小于** CONFIG_BT_DEVICE_NAME_MAX（通常为 16），即广播名称控制在 15 字符以内。