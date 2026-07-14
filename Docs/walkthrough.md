# Walkthrough - thinkpad_wireless Board HWMv2 Migration & Build Fixes

This walkthrough details the structural changes, config updates, encoding corrections, name shortening, ADC/USBD node enablement, CMake include path adjustments, and Devicetree bindings path registrations applied to migrate the custom `thinkpad_wireless` board to Hardware Model v2 (HWMv2) under the ZMK `main` branch.

## Changes Made

### 1. ZMK Dependency & Build Configuration
- **[west.yml](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/config/west.yml)**: Restored `revision: main` for the ZMK project, enabling support for Zephyr 4.1.0/HWMv2.
- **[module.yml](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/zephyr/module.yml)**:
  - Replaced the outdated `build: boards:` block with the standard Zephyr module setting `board_root: module` under `build: settings:`. Also registered `boards` at the root level for Twister.
  - Added `dts_root: module` under the `settings` block. This informs Zephyr's Devicetree compiler where to locate the custom Devicetree bindings (such as `gpio-ps2.yaml` and `zmk,input-mouse-ps2.yaml` under `module/dts/bindings/`). Without this, the DTS compiler fails to recognize custom properties and phandles (such as `ps2-device` in `/mouse_ps2` or `scl-gpios` in `/gpio_ps2`), causing compilation errors due to undefined `__device_dts_ord_...` phandle reference macros in the driver C files.

### 2. Board Directory & Kconfig Renaming
- Relocated the board files from `module/boards/thinkpad_wireless` to `module/boards/thinkpad/thinkpad_wireless` to follow the required `boards/<vendor>/<board_name>` nesting layout under HWMv2.
- Renamed the board Kconfig definition file from `Kconfig.board` to `Kconfig.thinkpad_wireless` (now located at `module/boards/thinkpad/thinkpad_wireless/Kconfig.thinkpad_wireless`).

### 3. Build & Code Integration Refinement
- **[CMakeLists.txt (board level)](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/boards/thinkpad/thinkpad_wireless/CMakeLists.txt)**: Updated the relative header inclusion path from `../../include` to `../../../include` to resolve ZMK application header resolution errors under the newly nested vendor directory level.
- **[CMakeLists.txt (module level)](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/CMakeLists.txt)**: Added `zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)` to expose ZMK's core application headers to the driver and input listener source files (e.g., `input_listener_ps2.c`). Without this, compiler errors occurred due to unresolved includes like `<zmk/endpoints.h>`.
- **[thinkpad_wireless.conf](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/config/thinkpad_wireless.conf)**:
  - Updated file path reference comments to the new nested directory structure.
  - Shortened `CONFIG_ZMK_KEYBOARD_NAME` from `"Thinkpad Wireless"` to `"ThinkpadWireless"` to satisfy ZMK's static assertion constraint (BLE local name max length limit: 16).
- **[thinkpad_wireless_defconfig](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless_defconfig)**:
  - Added `CONFIG_BT_DEVICE_NAME="ThinkpadWL"` to override the default board name GAP advertising name. Since the board name `"thinkpad_wireless"` (17 characters) exceeds the maximum length set by ZMK (`CONFIG_BT_DEVICE_NAME_MAX=16`), Zephyr's BT host initialization failed compile-time validation: `BUILD_ASSERT(DEVICE_NAME_LEN < CONFIG_BT_DEVICE_NAME_MAX)` (which requires the GAP name to be at most 15 characters). Overriding it to `"ThinkpadWL"` solves the build failure.
  - Added `CONFIG_ADC_NRFX_SAADC=y` to explicitly enable the Nordic SAADC driver for the battery divider.
- **[thinkpad_wireless.dts](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless.dts)**:
  - Enabled the `&adc` node (`status = "okay";`) at the end of the file. By default, the `adc` node in Nordic's `nrf52840.dtsi` is disabled. Without explicitly enabling it, Zephyr's SAADC driver Kconfig dependencies are not met, resulting in ZMK's battery sensor driver (`battery_voltage_divider.c`) failing to compile with `#error Unsupported ADC`.
  - Enabled the `&usbd` node (`status = "okay";`) at the end of the file. By default, the `usbd` USB controller node in Nordic's `nrf52840.dtsi` is disabled. Without explicitly enabling it, ZMK's USB stack cannot be enabled, which causes compilation/linking errors due to undefined references to ZMK USB helper functions (like `zmk_usb_get_conn_state`) in the application logic.
  - Renamed the deprecated `column-offset` property inside the `kscan_composite` node's `direct` child node to `col-offset` to satisfy the latest ZMK device tree bindings and silence the compiler warning.

### 4. CI Workflow Enhancements
- **[build.yml](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/.github/workflows/build.yml)**: Added path-based push and pull-request filters matching changes in `config/**`, `module/**`, `zephyr/**` to trigger compilation verification in GitHub Actions automatically upon code updates.

### 5. UTF-8 BOM Cleanliness & Coding Constraints
- **BOM Stripping**: Scanned the entire repository and stripped the UTF-8 BOM header (`0xEF, 0xBB, 0xBF`) from the following files to prevent Kconfig/YAML syntax errors in the Linux build environment:
  - `module/boards/thinkpad/thinkpad_wireless/Kconfig.defconfig`
  - `module/boards/thinkpad/thinkpad_wireless/Kconfig.thinkpad_wireless`
  - `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless.dts`
  - `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless_defconfig`
  - `module/CMakeLists.txt`
  - `module/Kconfig`
  - `Requirements/Requirements.md`
- **[development_log.md](file:///E:/Work/个人文档/业余研究/Thinkpad keyboard wireless/Docs/development_log.md)**:
  - Updated the log with v1.0.14, v1.0.15, v1.0.16, and v1.0.17 changelog entries.

---

## Validation Results

- **BOM Cleanliness Verification**: Verified that all source and config files in `module/` and `config/` are now free of BOM bytes.
- **Directory Layout Verification**: Confirmed that the new layout follows HWMv2 requirements.
- **DTS and Kconfig Dependency Verification**: Confirmed that the ADC and USBD nodes are now enabled, ensuring that the SAADC driver is correctly built and mapped to the battery sensor, and the ZMK USB device stack is successfully compiled.
- **Header Resolution Verification**: Confirmed that driver files under `module/` now have proper access to application include paths during compilation.
- **Devicetree Bindings Scope Verification**: Verified that custom DTS binding structures (such as `gpio-ps2` and `zmk,input-mouse-ps2`) are correctly mapped and identified by the Devicetree parser, resulting in correct dependency ordinal output in generated build headers.

---

## V1.01 Release - ZMK Studio Layouts, Battery Protection Cutoff, and Idle State Power-down

This release introduces critical battery protection improvements, ZMK Studio physical layouts support, and idle state power optimization.

### Changes Made

#### 1. ZMK Studio HWMv2 Layout Support
- **[NEW] [thinkpad_wireless-layouts.dtsi](file:///e:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless-layouts.dtsi)**: Created a dedicated layouts file describing a compliant zmk,physical-layout node with 	ransform and kscan parameters mapped to satisfy ZMK Studio keymap editing expectations.
- **[MODIFY] [thinkpad_wireless.dts](file:///e:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless.dts)**: Included 	hinkpad_wireless-layouts.dtsi and registered zmk,physical-layout = &physical_layout0; inside the chosen block to meet ZMK HWMv2 physical layout bindings requirements.

#### 2. Battery Protection Cutoff & Idle Power Savings
- **[MODIFY] [status_leds.c](file:///e:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/boards/thinkpad/thinkpad_wireless/status_leds.c)**:
  - **5V Boost Cutoff on Shutdown**: In the battery critical low-voltage detection routine, explicitly configured and pulled P0.12 (5V_EN) low before invoking sys_poweroff(). This forces the ETA1061 regulator to shutdown, ensuring that the TrackPoint is powered off and preventing battery over-discharge damage during shutdown.
  - **Idle State LED Control**: Subscribed to the zmk_activity_state_changed event and checked ZMK activity state. When entering ZMK_ACTIVITY_IDLE or ZMK_ACTIVITY_SLEEP, force the BT_LED (and battery indicator LEDs when on battery) off to satisfy the PRD requirement to dim all indicator LEDs except the power breathing LED during idle.

#### 3. Code Quality & Namespace Compliance
- **[MODIFY] [behavior_mouse_setting.c](file:///e:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/drivers/behavior_mouse_setting.c)**: Updated legacy include #include <drivers/behavior.h> to modern namespace #include <zephyr/drivers/behavior.h> in accordance with the project rules.

---

## V1.01 Validation Results
- **Compile Verification**: Confirmed that the workspace compiles without warnings or errors.
- **GPIO Functionality Verification**: Verified the P0.12 control call is successfully generated in assembly/driver.
- **Activity State Mapping**: Event subscriptions for activity state transitions are registered and updated asynchronously in the LED thread.
- **Git Sync Status**: Staged, committed, and successfully pushed to remote branch zmk-official-hwmv2-fix.

---

## V1.02 Release - Manual Power Toggle via PWRSWITCH (2s Power-On, 8s Power-Off, False-Wakeup Re-sleep)

This release implements a soft power toggle function using the PWRSWITCH button (P1.11) and nRF52840's GPREGRET retention register to solve the conflict with automatic deep sleep.

### Changes Made

#### 1. Early Boot Check & 2s Power-On (oard.c)
- **[MODIFY] [board.c](file:///e:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/boards/thinkpad/thinkpad_wireless/board.c)**:
  - Added #include <soc.h> and #include <zephyr/sys/poweroff.h>.
  - Checked NRF_POWER->GPREGRET on early boot (PRE_KERNEL_2).
  - If GPREGRET has the MANUAL_POWER_OFF_FLAG (0xAA), verify that the power button is held for 2 seconds.
  - If any other key woke the MCU, or if the user released the power button before 2 seconds, immediately cut power to 5V Boost and put the MCU back into System OFF.
  - If held for 2 seconds, clear the flag, flash the green battery LED 3 times, and boot normally.

#### 2. 8s Power-Off & LED Flashing (status_leds.c)
- **[MODIFY] [status_leds.c](file:///e:/Work/个人文档/业余研究/Thinkpad keyboard wireless/module/boards/thinkpad/thinkpad_wireless/status_leds.c)**:
  - Added #include <soc.h>.
  - Inside led_thread_fn, monitored the state of PWRSWITCH (P1.11).
  - If the button is held for 8 seconds (100 consecutive 80ms loop ticks), write the manual power-off flag (0xAA) to NRF_POWER->GPREGRET.
  - Flash all status LEDs (BT, Red, Green, Mute, Mic Mute, Caps Lock, Power Breathing LED) 3 times.
  - Cut power to 5V Boost (P0.12 = 0) and enter System OFF.

---

## V1.02 Validation Results
- **Compile Verification**: Confirmed compilation success without warnings.
- **Git Sync Status**: Staged, committed, and successfully pushed to remote branch zmk-official-hwmv2-fix.
