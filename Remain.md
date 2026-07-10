经过对整个代码仓（DTS、CMakeLists、Kconfig、C 驱动及应用层代码）的更深一层排查，并与产品需求文档（PRD）进行全面对标，我们发现了**更多隐藏的问题和尚未完全符合需求的设计细节**。

除了之前列出的几点外，这里是完整的**问题点**与**修改点**汇编（**依然遵循你的指令，只列出问题，不对代码做任何修改**）：

---

## 🔍 完整排查与发现清单

### 一、 设备树与 ZMK Studio 适配规范问题

#### 1. 物理布局 (Physical Layout) 缺失导致 ZMK Studio 冲突
*   **问题点**：根据 ZMK v4 (HWMv2) 规范及我们设定的约束条件，键盘必须遵循物理布局与电气矩阵分离的原则。目前 `thinkpad_wireless.dts` 的 `chosen` 容器下**缺失**了 `zmk,physical-layout` 属性配置，且板级目录中没有定义独立的 `<keyboard>-layouts.dtsi` 文件。这会导致固件无法兼容 ZMK Studio 的可视化键位编辑功能。
*   **修改点**：
    1.  在板级目录下新建 `thinkpad_wireless-layouts.dtsi` 文件，定义 `compatible = "zmk,physical-layout";` 节点，详细描述每个物理键的位置与大小。
    2.  在 `thinkpad_wireless.dts` 中引入该布局头文件，并在 `chosen` 节点中追加指定：
        ```dts
        chosen {
            zmk,physical-layout = &physical_layout0;
        };
        ```

---

### 二、 电源管理与低压/休眠保护逻辑问题（对照需求文档第 4 节）

#### 2. 紧急低压关机时未拉低 `5V_EN` 控制引脚（电池过放损坏风险，**P0 级隐患**）
*   **问题点**：需求文档第 4.1 节规定，在进入深休眠/关机时应“拉低 `5V_EN` (`P0.12` = 0V)，彻底关闭 5V Boost 电路”以达到微安级超低静态电流。然而，在 `status_leds.c` 的极低电量紧急关机逻辑中，代码仅仅关闭了指示灯，便直接调用了 `sys_poweroff()`，没有主动拉低外设供电使能引脚（`P0.12`）。由于 `sys_poweroff()` 可能会绕过 ZMK 正常的 `ext-power` 关断流程，该引脚在关机后可能保持悬空或高电平，导致 5V Boost 升压电路（ETA1061）持续工作，从而将电池完全放电至物理损坏。
*   **修改点**：
    - 在 `status_leds.c` 的紧急关机代码中，在调用 `sys_poweroff()` 之前，应获取 `ext_power` 设备或直接配置 GPIO `gpio0` 的 `12` 号引脚（`P0.12`），显式将其拉低为低电平（0V），切断 5V Boost。

#### 3. 电量采集检测频率不满足“每 10 秒运行一次”的要求
*   **问题点**：需求文档第 4.2 节要求“电池电压检测在系统启动时和运行过程中（每 10 秒）自动运行”。然而，目前 `config/thinkpad_wireless.conf` 中并未配置 ZMK 的电池采样上报间隔，这意味着固件将采用 ZMK 默认的 **60 秒** 轮询间隔，无法满足需求。
*   **修改点**：
    - 在 `config/thinkpad_wireless.conf` 中显式追加配置：
      ```kconfig
      CONFIG_ZMK_BATTERY_REPORT_INTERVAL=10
      ```

#### 4. 空闲状态 (Idle State) 下未关闭蓝牙指示灯 (BT_LED)
*   **问题点**：需求文档第 4.1 节规定，当键盘持续 **30 秒** 无任何按键进入空闲状态时，主控虽然保持蓝牙在线，但应当“关闭除电源指示外的所有指示 LED”。然而，目前 `status_leds.c` 的逻辑为：只要蓝牙处于连接状态，`BT_LED` 就始终保持常亮状态（`LED_ON(gpio1_dev, BT_LED_PIN)`），即便键盘已无操作数分钟。这会增加工作状态下的非必要功耗。
*   **修改点**：
    1.  在 `status_leds.c` 中订阅并监听 ZMK 的活动状态变更事件（`zmk_activity_state_changed`）。
    2.  当活动状态变为 `ZMK_ACTIVITY_IDLE` 时，关闭 `BT_LED`；当状态恢复为 `ZMK_ACTIVITY_ACTIVE` 且蓝牙依然连接时，重新点亮 `BT_LED`。

#### 5. 阶梯电压管理使用百分比 (SoC) 代替物理电压判定（精确度误差）
*   **问题点**：需求文档第 4.2 节要求根据“电池电压值”（低于 3.4V 关机保护、低于 3.5V 亮红灯、高于 3.5V 亮绿灯）作为明确阈值。而在 `status_leds.c` 中，判定逻辑是基于事件监听传回的电量百分比（以 `battery_soc < 2` 代表 3.4V，`battery_soc < 10` 代表 3.5V）。如果 ZMK 对电池放电曲线的换算表格存在偏差，或硬件分压阻值存在微小温漂，采用百分比阈值判定会导致物理电压关机点发生偏移。
*   **修改点**：
    - 建议在 `status_leds.c` 中直接调用 ADC 相关的 API 获取 `vbatt` 控制器上的原始物理电压（毫伏值）来进行阶梯判定；或者微调设备树中的分压器电阻参数，并在 ZMK 电池配置文件中映射出极为精确的百分比曲线。

---

### 三、 编码规范与头文件引用问题

#### 6. 自定义驱动文件使用了已废弃的旧版头文件引用路径
*   **问题点**：在自定义鼠标行为驱动 `module/drivers/behavior_mouse_setting.c` 的第 4 行，代码引用了旧版遗留的 API 头文件：
    ```c
    #include <drivers/behavior.h>
    ```
    这违反了我们在项目 `.agents/AGENTS.md` 与编译指南中设立的第 2.1 条约束：“引用 Zephyr 核心及外设头文件时，必须采用现代命名空间格式 `<zephyr/...>`”。虽然编译器可能在兼容层支持下通过，但这会在未来的 Zephyr SDK 升级中埋下编译中断的隐患。
*   **修改点**：
    - 将引用的路径修改为现代命名空间路径：
      ```c
      #include <zephyr/drivers/behavior.h>
      ```
