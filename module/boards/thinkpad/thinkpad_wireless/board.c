/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * board.c — Hardware-level initialization only.
 * All LED application logic lives in status_leds.c which uses the
 * ZMK event manager and can safely call ZMK subsystem APIs.
 */

#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>

#include "board_hw.h"

/* GPIO device handles via DTS node labels.
 * DEVICE_DT_GET() replaces the removed device_get_binding() in Zephyr 4.x. */
static const struct device *gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

/*
 * board_gpio_init - configure all board GPIO pins to safe initial states.
 *
 * All LEDs are active-LOW: GPIO_OUTPUT_HIGH = LED OFF at startup.
 * This must run early (PRE_KERNEL_2) so that pins are in a defined
 * state before the ZMK application layer starts.
 */
static int board_gpio_init(void) {
  if (!device_is_ready(gpio0_dev) || !device_is_ready(gpio1_dev)) {
    return -ENODEV;
  }

  /* ---- LED outputs: initial state = OFF (physical HIGH, active-LOW hw) ----
   * NOTE: the caps-lock LED (P0.31) is intentionally NOT configured here:
   * it is exclusively owned by the zmk,indicator-leds driver
   * (review 2026-08-13 item 2.4). */
  gpio_pin_configure(gpio1_dev, BT_LED_PIN, GPIO_OUTPUT_HIGH);
  gpio_pin_configure(gpio1_dev, BAT_LED_R_PIN, GPIO_OUTPUT_HIGH);
  gpio_pin_configure(gpio1_dev, BAT_LED_G_PIN, GPIO_OUTPUT_HIGH);
  gpio_pin_configure(gpio1_dev, MUTE_LED_PIN, GPIO_OUTPUT_HIGH);
  gpio_pin_configure(gpio1_dev, MIC_MUTE_LED_PIN, GPIO_OUTPUT_HIGH);

  /* ---- TrackPoint Reset pin via MOSFET inverter: initial state = RUN (MCU LOW -> TP_RST 5V) ---- */
  gpio_pin_configure(gpio1_dev, 9, GPIO_OUTPUT_LOW); /* P1.09 TP4_RESET: 0=RUN(5V), 1=RESET(0V) */

  /* ---- Input: charger interrupt (active LOW = charging) ---- */
  gpio_pin_configure(gpio0_dev, CHG_INT_PIN,
                     GPIO_INPUT | GPIO_PULL_UP); /* P0.08 CHG_INT (CoB) */

  /* ---- Manual Power-Off Wakeup Check ---- */
  if (NRF_POWER->GPREGRET == MANUAL_POWER_OFF_FLAG) {
    /* Wake-source diagnostics: PS 5.3.3 lists GPIO DETECT / LPCOMP / NFC /
     * VBUS(rising) as System OFF wake sources - RESETREAS tells which one
     * fired. printk works pre-scheduler; output needs a debug console. */
    printk("tpkb 0xAA wake: RESETREAS=0x%08x\n",
           (unsigned)NRF_POWER->RESETREAS);

    /* Configure PWRSWITCH (P1.11) as input pull-up */
    gpio_pin_configure(gpio1_dev, PWRSWITCH_PIN, GPIO_INPUT | GPIO_PULL_UP);

    /* 2026-08-19 fix (适配器充电/软件关机无法开机):
     *
     * Old code went straight back to System OFF when the power key was not
     * held at this exact instant. That races the user: after an 8 s hold the
     * key is usually still pressed during the shutdown animation, so the
     * GPIO DETECT signal (SENSE=LOW) is still high when sys_poweroff() runs
     * and the nRF52840 immediately wakes back up (PS: "Setting the system
     * to System OFF while DETECT is high will cause a wakeup from System
     * OFF reset"). The resulting wake loop only offers the user a ~100 ms
     * window to start a 2 s press, which is why "power on with a >2 s hold
     * after power off" failed in practice.
     *
     * Fix: instead of sleeping right away, wait up to PWR_CONFIRM_WINDOW_MS
     * for a deliberate power-button press (any wake source - key in bag,
     * USB VBUS edge, GPIOTE noise - is given the same chance). Only if no
     * press happens within the window do we go back to System OFF, which
     * keeps the bag-mis-press protection while decoupling power-on from the
     * boot-vs-finger race.
     */
#define PWR_CONFIRM_WINDOW_MS 8000
    bool pressed = false;
    for (int waited = 0; waited < PWR_CONFIRM_WINDOW_MS; waited += 100) {
      if (gpio_pin_get_raw(gpio1_dev, PWRSWITCH_PIN) == 0) {
        pressed = true;
        break;
      }
      k_busy_wait(100000); /* 100 ms busy wait */
    }

    if (!pressed) {
      /* No deliberate power-button press within the window: false wakeup
       * (e.g. key press in bag). Cut 5V Boost (P0.12) and sleep again. */
      gpio_pin_configure(gpio0_dev, BOOST_EN_PIN, GPIO_OUTPUT_LOW);
      gpio_pin_set(gpio0_dev, BOOST_EN_PIN, 0);
      printk("tpkb 0xAA: no power-key press in window, sleeping again\n");
      sys_poweroff();
    }

    /* User is holding the power switch, verify they hold it for 2 seconds */
    bool held = true;
    for (int i = 0; i < 20; i++) {
      k_busy_wait(100000); /* 100ms busy wait */
      if (gpio_pin_get_raw(gpio1_dev, PWRSWITCH_PIN) ==
          1) { /* Released early */
        held = false;
        break;
      }
    }

    if (!held) {
      /* Cut off 5V Boost (P0.12) */
      gpio_pin_configure(gpio0_dev, BOOST_EN_PIN, GPIO_OUTPUT_LOW);
      gpio_pin_set(gpio0_dev, BOOST_EN_PIN, 0);
      printk("tpkb 0xAA: power key released <2s, sleeping again\n");
      sys_poweroff();
    }

    printk("tpkb 0xAA: power key held 2s, booting\n");

    /* Power On success: clear manual power off flag */
    NRF_POWER->GPREGRET = 0;

    /* Sequential turn-on: BT, Green, Red, Mic Mute, Mute */
    /* (Caps lock LED is owned by the indicator driver, review 2.4) */
    gpio_pin_set_raw(gpio1_dev, BT_LED_PIN, 0); /* BT ON */
    k_busy_wait(150000);
    gpio_pin_set_raw(gpio1_dev, BAT_LED_G_PIN, 0); /* Battery Green ON */
    k_busy_wait(150000);
    gpio_pin_set_raw(gpio1_dev, BAT_LED_R_PIN, 0); /* Battery Red ON */
    k_busy_wait(150000);
    gpio_pin_set_raw(gpio1_dev, MIC_MUTE_LED_PIN, 0); /* Mic Mute ON */
    k_busy_wait(150000);
    gpio_pin_set_raw(gpio1_dev, MUTE_LED_PIN, 0); /* Mute ON */
    k_busy_wait(300000);

    /* Turn all OFF */
    gpio_pin_set_raw(gpio1_dev, BT_LED_PIN, 1);
    gpio_pin_set_raw(gpio1_dev, BAT_LED_G_PIN, 1);
    gpio_pin_set_raw(gpio1_dev, BAT_LED_R_PIN, 1);
    gpio_pin_set_raw(gpio1_dev, MIC_MUTE_LED_PIN, 1);
    gpio_pin_set_raw(gpio1_dev, MUTE_LED_PIN, 1);
  }

  return 0;
}

SYS_INIT(board_gpio_init, PRE_KERNEL_2, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
