# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT
#
# CMakeLists.txt for the thinkpad_wireless board.
# This only compiles hardware-level board initialization files.
# PS/2 TrackPoint drivers are compiled via the module/ directory.

zephyr_library()

# Include ZMK application headers for status_leds.c event listeners
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/module/include)

zephyr_library_sources_ifdef(CONFIG_BOARD_THINKPAD_WIRELESS board.c)
zephyr_library_sources_ifdef(CONFIG_BOARD_THINKPAD_WIRELESS status_leds.c)
