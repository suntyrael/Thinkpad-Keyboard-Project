/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * board_hw.h - Single source of truth for board-level GPIO pin definitions
 * and the manual power-off flag shared by board.c (PRE_KERNEL init) and
 * status_leds.c (application LED thread).
 *
 * Previously MANUAL_POWER_OFF_FLAG and the LED pin numbers were duplicated in
 * both files, so a change in one could silently miss the other
 * (review 2026-08-13 items 2.4 / 3.4).
 */

#ifndef THINKPAD_WIRELESS_BOARD_HW_H
#define THINKPAD_WIRELESS_BOARD_HW_H

/* Manual power-off marker stored in NRF_POWER->GPREGRET so that the next
 * wakeup requires a deliberate 2-second power-button hold (see board.c).
 * Also written before low-battery shutdown so a bag-accidental wake does not
 * drain the cell (review 2026-08-13 item 2.18). */
#define MANUAL_POWER_OFF_FLAG 0xAA

/* ---- Status LEDs (all active-LOW: 0 = on, 1 = off), gpio1 ---- */
#define BT_LED_PIN 2       /* P1.02  Bluetooth status */
#define BAT_LED_R_PIN 6    /* P1.06  Battery/charger red */
#define BAT_LED_G_PIN 4    /* P1.04  Battery/charger green */
#define MUTE_LED_PIN 15    /* P1.15  Speaker mute */
#define MIC_MUTE_LED_PIN 7 /* P1.07  Mic mute */
#define CAPS_LOCK_LED_PIN                                                      \
  31 /* P0.31  Caps lock (gpio0, driven by the                                 \
      * zmk,indicator-leds driver - do NOT touch it                            \
      * manually, review 2026-08-13 item 2.4) */

/* ---- Misc board IO ---- */
#define CHG_INT_PIN 3    /* P1.03  Charger interrupt (LOW = charging) */
#define PWRSWITCH_PIN 11 /* P1.11  Power switch (active-LOW) */
#define HOTKEY_PIN 8     /* P1.08  ThinkVantage key (active-LOW) */
#define BOOST_EN_PIN 12  /* P0.12  5V boost enable (active-HIGH, gpio0) */

#endif /* THINKPAD_WIRELESS_BOARD_HW_H */
