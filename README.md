# ThinkPad Keyboard Wireless Project

本项目致力于将联想 **ThinkPad X220 / T420** 的经典键盘（使用 BTB 44-pin 接口）转换为支持 **蓝牙/USB 双模** 的无线键盘。

主控采用 **nRF52840-QIAA**，固件运行于开源的 **ZMK Firmware** 系统。

---

## 📂 目录结构说明

*   **Code/**：包含 ZMK 固件相关的配置（zmk-config）、DeviceTree（.dts）定义及键位映射（.keymap）。
*   **Docs/**：收集的各类芯片数据手册、主板原理图、小红帽（TrackPoint）接口协议、接口说明等参考文档。
*   **Hardware/**：硬件设计原理图（PDF 预览文件等）。
*   **Requirements/**：项目需求细节、键盘矩阵引脚测绘及 GPIO 映射表（Excel）。

---

## ⚡ 硬件架构与主要器件

1.  **主控芯片**：**Nordic nRF52840-QIAA-R0** (aQFN73 / QFN73 封装)，提供 48 个 GPIO，具备极低的蓝牙功耗。
2.  **供电与 LDO**：
    *   主控采用 **RT9080-33GJ5** (或 SGM2036-3.3) 3.3V LDO，其静态功耗 $ 仅为 **2µA**，最大支持 600mA 输出，非常适合低功耗蓝牙外设。
    *   主控供电配置为 Normal Voltage 模式（短接 VDDH 与 VDD，禁用内部 REG0 高压调节器）。
3.  **充电芯片**：**TP4054-42-SOT25R** 线性充电芯片 (SOT-25-5 封装)，充电电流根据 PROG 电阻设置（如 2k 对应 500mA）。
4.  **BTB 连接器**：**Molex 54363-0489** (44-Pin 板对板连接器公座)，用于精确匹配 X220 键盘端的 44-pin 排线。
5.  **电量检测**：通过 10M / 10M 电阻分压网络，将电池电压送入 `P0.02` 引脚（AIN0，SAADC 通道 0），利用 SAADC 进行电压读取与电量计算。
6.  **5V Boost 升压控制**：新增了 `5V_EN` 网络连接至 `P0.12`，用作 5V 升压电路的使能端（为 5V 小红帽和 T61 键盘提供兼容备份）。在固件中配置了 ZMK 的 Ext-Power（外部电源控制），使得键盘进入休眠时能自动拉低 `P0.12` 关断 5V Boost，唤醒时再自动拉高开启，实现极低的待机功耗。

---

## 🔌 引脚映射与电路连接

以下是经原理图 `SCH_Schematic1_2026-07-09.pdf` 最终确认的信号与物理接口引脚映射关系。包含了 `U2` (X220 BTB 键盘座)、`FPC2` (T61 FPC 键盘座) 以及板级信号转接排线 `FPC3` 的管脚对应。

### 1. 键盘矩阵与外设 GPIO 映射表

| 信号名称 | MCU引脚 (GPIO) | MCU物理球位 (Ball) | FPC3引脚 (主控板端) | U2引脚 (X220 BTB) | FPC2引脚 (转接板端) | 作用与配置说明 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **KEY_SENSE0** | `P0.26` | `G1` | Pin 17 | Pin 5 | Pin 24 | 矩阵行 0 读取，`row-gpios` 分配 |
| **KEY_SENSE1** | `P0.28` | `B11` | Pin 13 | Pin 13 | Pin 28 | 矩阵行 1 读取，`row-gpios` 分配 |
| **KEY_SENSE2** | `P0.05` | `K2` | Pin 15 | Pin 9 | Pin 26 | 矩阵行 2 读取，`row-gpios` 分配 |
| **KEY_SENSE3** | `P0.04` | `J1` | Pin 16 | Pin 7 | Pin 25 | 矩阵行 3 读取，`row-gpios` 分配 |
| **KEY_SENSE4** | `P0.27` | `H2` | Pin 14 | Pin 11 | Pin 27 | 矩阵行 4 读取，`row-gpios` 分配 |
| **KEY_SENSE5** | `P0.07` | `M2` | Pin 18 | Pin 3 | Pin 23 | 矩阵行 5 读取，`row-gpios` 分配 |
| **KEY_SENSE6** | `P1.12` | `B17` | Pin 11 | Pin 17 | Pin 30 | 矩阵行 6 读取，`row-gpios` 分配 |
| **KEY_SENSE7** | `P1.14` | `B15` | Pin 12 | Pin 15 | Pin 29 | 矩阵行 7 读取，`row-gpios` 分配 |
| **KEY_DRV0** | `P0.13` | `AD8` | Pin 30 | Pin 22 | Pin 11 | 矩阵列 0 驱动，`col-gpios` 分配 |
| **KEY_DRV1** | `P0.20` | `AD16` | Pin 32 | Pin 18 | Pin 9 | 矩阵列 1 驱动，`col-gpios` 分配 |
| **KEY_DRV2** | `P0.22` | `AD18` | Pin 34 | Pin 14 | Pin 7 | 矩阵列 2 驱动，`col-gpios` 分配 |
| **KEY_DRV3** | `P0.24` | `AD20` | Pin 36 | Pin 10 | Pin 5 | 矩阵列 3 驱动，`col-gpios` 分配 |
| **KEY_DRV4** | `P1.01` | `Y23` | Pin 40 | Pin 2 | Pin 1 | 矩阵列 4 驱动，`col-gpios` 分配 |
| **KEY_DRV5** | `P0.25` | `AC21` | Pin 39 | Pin 4 | Pin 2 | 矩阵列 5 驱动，`col-gpios` 分配 |
| **KEY_DRV6** | `P1.00` | `AD22` | Pin 37 | Pin 8 | Pin 4 | 矩阵列 6 驱动，`col-gpios` 分配 |
| **KEY_DRV7** | `P0.21` | `AC17` | Pin 35 | Pin 12 | Pin 6 | 矩阵列 7 驱动，`col-gpios` 分配 |
| **KEY_DRV8** | `P0.23` | `AC19` | Pin 38 | Pin 6 | Pin 3 | 矩阵列 8 驱动，`col-gpios` 分配 |
| **KEY_DRV9** | `P0.16` | `AC11` | Pin 31 | Pin 20 | Pin 10 | 矩阵列 9 驱动，`col-gpios` 分配 |
| **KEY_DRV10** | `P0.19` | `AC15` | Pin 33 | Pin 16 | Pin 8 | 矩阵列 10 驱动，`col-gpios` 分配 |
| **KEY_DRV11** | `P0.15` | `AD10` | Pin 29 | Pin 24 | Pin 12 | 矩阵列 11 驱动，`col-gpios` 分配 |
| **KEY_DRV12** | `P0.14` | `AC9` | Pin 27 | Pin 28 | Pin 14 | 矩阵列 12 驱动，`col-gpios` 分配 |
| **KEY_DRV13** | `P1.05` | `T23` | Pin 25 | Pin 32 | Pin 16 | 矩阵列 13 驱动，`col-gpios` 分配 |
| **KEY_DRV14** | `P0.17` | `AD12` | Pin 28 | Pin 26 | Pin 13 | 矩阵列 14 驱动，`col-gpios` 分配 |
| **KEY_DRV15** | `P1.03` | `V23` | Pin 26 | Pin 30 | Pin 15 | 矩阵列 15 驱动，`col-gpios` 分配 |
| **TP4CLK** | `P1.13` | `A16` | Pin 3 | Pin 39 | Pin 38 | 小红帽时钟端，PS/2 接口 |
| **TP4DATA** | `P1.10` | `A20` | Pin 2 | Pin 37 | Pin 39 | 小红帽数据端，PS/2 接口 |
| **TP4_RESET** | `P1.09` | `R1` | Pin 21 | Pin 40 (via R15) | Pin 20 | 小红帽复位信号 |
| **LEDCPSLOCK** | `P0.31` | `A8` | Pin 9 | Pin 21 (via R18) | Pin 32 | 大写锁定 (Caps Lock) 指示灯 (低电平点亮) |
| **LEDPWR** | `P0.29` | `A10` | Pin 8 | Pin 23 (via R19) | Pin 33 | 电源状态指示灯 (低电平点亮) |
| **-LED_MUTE** | `P1.15` | `A14` | Pin 7 | Pin 33 (via R17) | Pin 34 | 扬声器静音指示灯 (低电平点亮) |
| **-LEDMICMUTE_R** | `P1.07` | `P23` | Pin 23 | Pin 36 (via R16) | Pin 18 | 麦克风静音指示灯 (低电平点亮) |
| **BT_LED** | `P1.02` | `W24` | - | - | - | 蓝牙配对与状态指示灯 (低电平点亮) |
| **BAT_LED_R** | `P1.06` | `R24` | - | - | - | 充电指示红灯 (低电平点亮) |
| **BAT_LED_G** | `P1.04` | `U24` | - | - | - | 充满/充电绿灯 (低电平点亮) |
| **5V_EN** | `P0.12` | `U1` | - | - | - | 5V Boost 升压使能端 (高电平开启) |
| **BAT_ADC** | `P0.02` | `A12` | - | - | - | 电池电压采集 (AIN0) |
| **CHG_INT** | `P0.08` | `N1` | - | - | - | 充电状态中断读取 |
| **-PWRSWITCH** | `P1.11` | `B19` | Pin 10 | Pin 19 | Pin 31 | 电源按键输入 (低电平触发) |
| **-HOTKEY** | `P1.08` | `P2` | Pin 19 | Pin 1 | Pin 22 | ThinkVantage 按键输入 (低电平触发) |
| **VDD3V3** | - | - | Pin 5 | Pin 36 | Pin 35 | 3.3V 系统电源供电网络 |
| **VDD3V3/5V (Selectable)** | - | - | Pin 22 | Pin 19 | Pin 38 | 可选系统主电源（由 R22 (0R) 选 5V，R23 (NC) 选 3.3V） |
| **GND** | - | - | Pin 1, 4, 6, 20, 24, 41, 42 | Pin 31, 34, 41-44 | Pin 17, 21, 35, 37, 40-42 | 公共接地端 |

---

## 📦 核心元器件选型总结

1.  **主控 MCU**：`nRF52840-QIAA-R0` (aQFN73 封装)。支持蓝牙 5.4 和 USB 2.0，提供 48 个 GPIO。配置为 Normal Voltage 模式，短接 VDDH 与 VDD，关闭内部高电压调节器 (REG0)。
2.  **稳压 LDO**：`RT9080-33GJ5` (或 SGM2036-3.3)，TSOT-23-5 封装。超低静态电流 **Iq = 2µA**，最大持续输出电流 **600mA**，低压差 (dropout)，完美契合低功耗无线设计。
3.  **锂电池充电芯片**：`TP4054-42-SOT25R` (SOT-25-5 封装)，线性充电芯片。PROG 电阻配合为 **2k ohm**，设定 500mA 恒流充电（不可使用不匹配的 51k ohm 电阻）。
4.  **5V Boost 升压芯片**：`ETA1061V50S2G` (SOT-23-6 封装)，高效同步整流 DC-DC 升压芯片。提供 5.0V 稳定输出（兼容 5V 小红帽和 T61 键盘供电）。具备超低静态电流与 **True Shutdown (真关断)** 功能，在 EN 拉低时完全阻断并隔离负载与输入，避免产生静态漏电。
5.  **ESD 接口保护**：`PESD5V0U2BT,215` (SOT-23 封装)，用于 USB 数据线及小红帽 PS/2 数据线防静电保护。
6.  **时钟晶振**：
    *   高频外部无源晶振：`32MHz`（封装 2520，精度 $\pm$10ppm）。
    *   低频外部无源晶振：`32.768kHz`（封装 3215，精度 $\pm$20ppm）。
7.  **物理连接器接口**：
    *   **U2 (X220 BTB Receptacle)**：`Molex 54363-0489` (40-Pin 双排 BTB 母座，另含 4 个屏蔽脚 41-44)。
    *   **FPC2 (T61 FPC Receptacle)**：`AFC01-S40FCA-00` (40-Pin FPC 插座，0.5mm 间距，下接插类型)。
    *   **FPC3 (板级连接器)**：`AFC01-S40FCA-00` (40-Pin FPC 插座)。
    *   **USB 接口**：16-pin Type-C 母座，CC1 与 CC2 必须各自接 **5.1k ohm** 下拉电阻到 GND 以兼容 C-to-C 握手。

---

## ⌨️ 固件与控制逻辑 (ZMK)

ZMK 固件通过以下组合按键支持设备管理与电源控制：

*   **5V Boost 动态休眠电源策略**：
    *   **空闲状态**（无按键 30 秒）：蓝牙维持连接，关闭 LED 指示灯，5V Boost 保持供电以维持小红帽瞬时响应。
    *   **深度休眠**（无按键 15 分钟）：断开蓝牙连接，主控进入 System Off，拉低 `P0.12`（5V_EN = 0）彻底关断 5V Boost，切断小红帽与外部端口供电，待机电流降至约 2µA。
    *   **唤醒机制**：按键盘**任意按键**触发 GPIO 中断唤醒，主控立即重新使能 5V Boost 并发起蓝牙回连。如果 PC 处于关机或休眠状态，键盘也会在无按键操作 15 分钟后正常休眠。
*   **蓝牙配对模式**：使用组合键 **Hotkey + Power Switch** 进入配对广播状态。
*   **多槽位蓝牙设备切换**：使用 **Hotkey + 1 / 2 / 3 / 4 / 5** 切换到不同的蓝牙配对配置文件 (Profile 0 - 4)。
*   **双模切换**：支持 USB 与蓝牙双模，自动检测 USB 连接状态并优先使用有线传输。