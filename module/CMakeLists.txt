# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT
#
# CMakeLists.txt for the thinkpad-wireless PS/2 TrackPoint module.
# This module provides PS/2 GPIO/UART drivers, mouse input handling,
# and related behaviors for use with ZMK firmware.

zephyr_library()

# Include the module's public headers
zephyr_library_include_directories(include)

# PS/2 transport layer drivers
zephyr_library_sources_ifdef(CONFIG_PS2_GPIO     drivers/ps2_gpio.c)
zephyr_library_sources_ifdef(CONFIG_PS2_UART     drivers/ps2_uart.c)

# ZMK TrackPoint / PS/2 mouse input subsystem
zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_MOUSE_PS2        drivers/input_mouse_ps2.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_LISTENER_PS2     drivers/input_listener_ps2.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_BEHAVIOR_MOUSE_SETTING drivers/behavior_mouse_setting.c)
