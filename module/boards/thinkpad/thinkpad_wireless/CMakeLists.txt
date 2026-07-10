# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT
#
# CMakeLists.txt for the thinkpad_wireless board.
# Now located inside the module (module/boards/thinkpad_wireless/).
# Compiles only hardware-level board initialization files.
# PS/2 TrackPoint drivers are compiled from the module root (module/).

zephyr_library()

# Include ZMK application headers for status_leds.c event listeners
# ${CMAKE_CURRENT_LIST_DIR}/../../../include = module/include
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../include)

zephyr_library_sources_ifdef(CONFIG_BOARD_THINKPAD_WIRELESS board.c)
zephyr_library_sources_ifdef(CONFIG_BOARD_THINKPAD_WIRELESS status_leds.c)
