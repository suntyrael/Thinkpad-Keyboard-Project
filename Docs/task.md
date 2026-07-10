# HWMv2 Migration & BOM / BLE Name / ADC Enablement / CMake Include Tasks

- `[x]` Modify `config/west.yml` to set `revision` to `main`
- `[x]` Modify `zephyr/module.yml` to register `board_root: module` and add Twister `boards` list
- `[x]` Relocate board files to `module/boards/thinkpad/thinkpad_wireless/`
- `[x]` Rename `Kconfig.board` to `Kconfig.thinkpad_wireless` in the board directory
- `[x]` Update relative path in `module/boards/thinkpad/thinkpad_wireless/CMakeLists.txt` to `../../../include`
- `[x]` Add `zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)` in `module/CMakeLists.txt` to solve the module-level driver compilation error (missing ZMK application headers)
- `[x]` Update comments in `config/thinkpad_wireless.conf` pointing to the old path
- `[x]` Add `push` and `pull_request` path-based triggers to `.github/workflows/build.yml`
- `[x]` Scan all files in the repository and strip UTF-8 BOM bytes (`0xEF, 0xBB, 0xBF`)
- `[x]` Shorten `CONFIG_ZMK_KEYBOARD_NAME` in `config/thinkpad_wireless.conf` to `"ThinkpadWireless"` (<=16 characters) to pass BLE static assertion
- `[x]` Add `CONFIG_BT_DEVICE_NAME="ThinkpadWL"` (<=15 characters) in `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless_defconfig` to prevent Zephyr GAP name length static assertion error
- `[x]` Enable `&adc` node in `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless.dts` to solve the battery voltage divider compilation error (`Unsupported ADC`)
- `[x]` Explicitly enable `CONFIG_ADC_NRFX_SAADC=y` in `module/boards/thinkpad/thinkpad_wireless/thinkpad_wireless_defconfig`
- `[x]` Rename deprecated `column-offset` to `col-offset` inside the composite kscan node in `thinkpad_wireless.dts`
- `[x]` Add "Coding Constraints" section in `Docs/development_log.md` (covering BOM restriction, LF format, and BLE/GAP name length limits)
- `[x]` Add `v1.0.12`, `v1.0.13`, `v1.0.14`, and `v1.0.15` changelog entries in `Docs/development_log.md`
- `[x]` Push all changes to GitHub branch `zmk-official-hwmv2-fix`
