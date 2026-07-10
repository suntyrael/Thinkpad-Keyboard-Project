# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

zephyr_library()

# Include ZMK application headers scoped to this board library only
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/module/include)

zephyr_library_sources_ifdef(CONFIG_BOARD_THINKPAD_WIRELESS board.c)
zephyr_library_sources_ifdef(CONFIG_BOARD_THINKPAD_WIRELESS status_leds.c)

# TrackPoint driver compilation
zephyr_library_include_directories(include)

zephyr_library_sources_ifdef(CONFIG_PS2_GPIO drivers/ps2_gpio.c)
zephyr_library_sources_ifdef(CONFIG_PS2_UART drivers/ps2_uart.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_MOUSE_PS2 drivers/input_mouse_ps2.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_LISTENER_PS2 drivers/input_listener_ps2.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_BEHAVIOR_MOUSE_SETTING drivers/behavior_mouse_setting.c)
