---
name: zmk-build-helper
description: 帮助排查与预防 ZMK / Zephyr 固件编译中的常见错误（包括编码格式、HWMv2、DTS 依赖、链接引用及静态断言限制等）。
---

# ZMK / Zephyr 固件编译助手与编码防错规范

本 Skill 用于指导在进行 ZMK 固件及 Zephyr 底层驱动开发时，关于法律合规、代码规范、设备树（DTS）结构、ZMK Studio 适配、构建路径及外设节点使用等方面的基本编码要求与防错策略。

## 1. 法律合规与基本编码规范

### 1.1 Clean Room 净室设计原则 (MIT 许可合规)
ZMK 使用宽松的 MIT 许可证。为了保持代码库的合规性，任何为 ZMK 开发的代码（包括自定义指点杆/小红点驱动、监听器、板级初始化）都必须严格遵守：
*   **禁止抄袭/参考 GPL 代码**：严禁直接引用、修改或详细查阅任何 GPL 许可项目的源码（如 QMK、TMK 固件）。
*   **允许做法**：阅读公共文档、技术白皮书、硬件芯片手册，或查看现有物理布局的坐标（如 QMK 配置文件里的矩阵排布图），在此基础上进行独立的代码实现。

### 1.2 代码格式化工具
*   **C/C++ 代码**：所有的 .c 和 .h 源文件必须在提交前使用 clang-format 进行格式化。
*   **Markdown 说明文档**：所有的 .md 说明文档在提交前应使用 Prettier 格式化。

### 1.3 头文件包含命名空间格式
在 Zephyr 3.x/4.x 及更高版本中，标准的内核和驱动头文件应使用 <zephyr/...> 命名空间。
*   **规范用法**：
    `c
    #include <zephyr/kernel.h>
    #include <zephyr/device.h>
    #include <zephyr/drivers/gpio.h>
    #include <zephyr/sys/util.h>
    `
*   **避免用法**：不推荐使用无 zephyr/ 前缀的遗留路径（如 #include <kernel.h>），以防未来编译报错。

### 1.4 文件编码格式 (UTF-8 without BOM & LF)
*   **规范**：所有源文件（.c、.h）、设备树（.dts、.dtsi、.yaml）、编译配置（CMakeLists.txt、Kconfig、.conf、defconfig）必须以 **UTF-8 无 BOM (without BOM)** 格式编码保存，并使用 **LF** 换行符。

---

## 2. 设备树（DTS）与 ZMK Studio 适配规范

自 ZMK v4 (HWMv2) 起，为了实现对 ZMK Studio（键盘键位可视化实时编辑器）的兼容，对设备树提出了更清晰的结构划分要求。

### 2.1 物理布局 (Physical Layouts) 与 Chosen 节点注册
*   **分离规范**：键盘的物理按键位置与矩阵电路应进行分离描述。
*   **定义要求**：物理按键布局应定义在独立的 <keyboard>-layouts.dtsi 中，使用 compatible = "zmk,physical-layout";。
*   **所选属性 (Chosen)**：在板级 .dts 的 chosen 节点中，必须指定 zmk,physical-layout：
    `dts
    chosen {
        zmk,physical-layout = &physical_layout0;
    };
    `

### 2.2 矩阵变换 (Matrix Transform)
*   当键盘的物理键位布局（Row/Col）与 KSCAN 的电气引脚扫频坐标不一致时，必须定义 zmk,matrix-transform 节点，并在 chosen 属性中注册：
    `dts
    chosen {
        zmk,matrix-transform = &default_transform;
    };
    `

### 2.3 自定义行为 (Behaviors) 与 组合键 (Combos)
*   **自定义行为**：Hold-Tap、Tap-Dance、Mod-Morph 和 Macro 必须放置在设备树的 ehaviors 节点下。
*   **Hold-Tap 规范**：
    - 必须指定 compatible = "zmk,behavior-hold-tap";。
    - 必须显式设定 lavor（如 "hold-preferred"、"tap-preferred"）和 	apping-term-ms。
    - 绑定槽格个数属性 #binding-cells 必须设为 <2>。
*   **组合键 (Combos)**：
    - 必须在设备树根节点的 combos 容器中定义，并使用 key-positions 数组指定触发该 Combo 的物理按键索引值。

---

## 3. 编译系统与模块路径规范

### 3.1 模块编译隔离与 CMake 相对路径
在 Zephyr 中，自定义文件夹通常会被构建为一个独立的模块（库 target）。该模块无法直接继承主应用工程的全局头文件搜索路径。
*   **规范配置**：在模块本地的 CMakeLists.txt 中，使用 zephyr_library_include_directories 显式引入主应用的包含目录，必须使用基于 CMake 变量的相对路径：
    `cmake
    # 显式暴露 ZMK 应用层头文件给模块内的驱动源文件
    zephyr_library_include_directories(/include)
    `
*   **禁止做法**：严禁在 CMakeLists.txt 中使用硬编码的宿主机绝对路径。

### 3.2 板级嵌套结构 (HWMv2) 与 CMake 相对路径
当适配 ZMK 新版分支时，板级文件往往需要迁移到 HWMv2 厂商嵌套路径：oards/<vendor>/<board_name>。
*   **注意事项**：由于文件目录深度相比 HWMv1 增加了一级，板级 CMakeLists.txt 中包含上层应用目录的相对路径必须进行对应调整。
    - HWMv1 路径示例：../../include
    - HWMv2 嵌套路径示例：../../../include

### 3.3 自定义 DTS YAML 绑定的检索注册
当在自定义外设驱动中使用新的 compatible 属性时，编译器必须能找到对应的 YAML 格式的属性绑定（DTS Bindings）定义。
*   **要求**：如果绑定文件存放在自定义模块下（如 module/dts/bindings/），必须在 zephyr/module.yml 中向内核编译器注册该搜索路径，否则编译驱动时会报 __device_dts_ord_... 未定义的宏错误。
*   **规范配置**：
    `yaml
    # zephyr/module.yml
    build:
      settings:
        board_root: module
        dts_root: module
    `

---

## 4. 外设与硬件控制器使用规范

在使用任何底层外设（如 GPIO、ADC、PWM, USB, SPI, I2C）编写高层应用或驱动逻辑时，必须确保“硬件节点使能”与“驱动软件编译”同时处于就绪状态。

### 4.1 外设就绪检查清单 (DTS & Kconfig)
当代码中使用了某个物理外设：
1.  **DTS 状态使能**：
    在板级设备树（.dts）中显式将该控制器节点的状态设为 "okay"。
    `dts
    &adc { status = "okay"; };
    &usbd { status = "okay"; };
    `
    *原因*：在 SOC 定义的基准设备树中，外设控制器的 status 属性默认都是 "disabled"。若未显式启用，DTS 编译器不会生成对应的节点 ORD 符号。
2.  **Kconfig 驱动使能**：
    在板级 _defconfig 或项目 .conf 中，使能对应的硬件驱动宏。
    `kconfig
    CONFIG_ADC=y
    CONFIG_ADC_NRFX_SAADC=y
    CONFIG_USB_DEVICE_STACK=y
    `
    *原因*：缺少 Kconfig 驱动使能，即使设备树配置了该节点，内核驱动代码也不会参与编译，导致应用层调用 DEVICE_DT_GET 获取设备时报符号丢失。

### 4.2 外设依赖防错排查表

| 使用场景 | 代码中依赖的头文件/函数 | 必须使能的 DTS 节点 | 必须启用的 Kconfig 宏 |
| :--- | :--- | :--- | :--- |
| **电量检测 (ADC)** | <zephyr/drivers/adc.h> | &adc { status = "okay"; }; | CONFIG_ADC=y<br>CONFIG_ADC_NRFX_SAADC=y |
| **USB 状态/协议栈** | <zmk/usb.h> <br> zmk_usb_is_powered() | &usbd { status = "okay"; }; | CONFIG_USB_DEVICE_STACK=y |
| **中断触发 (GPIO)** | <zephyr/drivers/gpio.h> | &gpiote { status = "okay"; }; | CONFIG_GPIO=y |
| **指示灯 (PWM)** | <zephyr/drivers/pwm.h> | &pwm0 { status = "okay"; }; | CONFIG_PWM=y |

---

## 5. 字符串与名称静态断言长度限制

在配置蓝牙设备信息时，需要注意长度限制，避免触发底层的 BUILD_ASSERT。
1.  **CONFIG_ZMK_KEYBOARD_NAME**：在 .conf 中配置的键盘名称长度**不得超过 16 个字符**。
    - 原因：ZMK le.c 包含 BUILD_ASSERT(sizeof(CONFIG_ZMK_KEYBOARD_NAME) - 1 <= 16) 静态校验。
2.  **CONFIG_BT_DEVICE_NAME**：在 _defconfig 中配置的 GAP 设备广播名称，长度必须**小于** CONFIG_BT_DEVICE_NAME_MAX。
    - 原因：Zephyr 蓝牙栈初始化时要求 GAP 广播名长度预留至少 1 字节作为结束符，否则无法通过初始化编译。