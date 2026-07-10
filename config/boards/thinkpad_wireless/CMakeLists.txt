#
# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT
#

zephyr_sources(board.c)
zephyr_sources_ifdef(CONFIG_ZMK_BLE status_leds.c)