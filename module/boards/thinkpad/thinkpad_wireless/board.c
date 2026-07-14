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
#define MANUAL_POWER_OFF_FLAG 0xAA

static int board_gpio_init(void) {
  if (!device_is_ready(gpio0_dev) || !device_is_ready(gpio1_dev)) {
    return -ENODEV;
  }

  /* ---- LED outputs: initial state = OFF (physical HIGH, active-LOW hw) ----
   */
  gpio_pin_configure(gpio1_dev, 2, GPIO_OUTPUT_HIGH); /* P1.02  BT LED */
  gpio_pin_configure(gpio1_dev, 6,
                     GPIO_OUTPUT_HIGH); /* P1.06  Battery Red LED  */
  gpio_pin_configure(gpio1_dev, 4,
                     GPIO_OUTPUT_HIGH); /* P1.04  Battery Green LED*/
  gpio_pin_configure(gpio1_dev, 15, GPIO_OUTPUT_HIGH); /* P1.15  Mute LED */
  gpio_pin_configure(gpio1_dev, 7, GPIO_OUTPUT_HIGH);  /* P1.07  Mic Mute LED  */

  /* ---- Input: charger interrupt (active LOW = charging) ---- */
  gpio_pin_configure(gpio0_dev, 8,
                     GPIO_INPUT | GPIO_PULL_UP); /* P0.08 CHG_INT */

  /* ---- Manual Power-Off Wakeup Check ---- */
  if (NRF_POWER->GPREGRET == MANUAL_POWER_OFF_FLAG) {
    /* Configure PWRSWITCH (P1.11) as input pull-up */
    gpio_pin_configure(gpio1_dev, 11, GPIO_INPUT | GPIO_PULL_UP);

    /* If power switch is not pressed (high), it's a false wakeup (e.g. key
     * press in bag) */
    if (gpio_pin_get_raw(gpio1_dev, 11) == 1) {
      /* Cut off 5V Boost (P0.12) just in case */
      gpio_pin_configure(gpio0_dev, 12, GPIO_OUTPUT_LOW);
      gpio_pin_set(gpio0_dev, 12, 0);
      sys_poweroff();
    }

    /* User is holding the power switch, verify they hold it for 2 seconds */
    bool held = true;
    for (int i = 0; i < 20; i++) {
      k_busy_wait(100000);                        /* 100ms busy wait */
      if (gpio_pin_get_raw(gpio1_dev, 11) == 1) { /* Released early */
        held = false;
        break;
      }
    }

    if (!held) {
      /* Cut off 5V Boost (P0.12) */
      gpio_pin_configure(gpio0_dev, 12, GPIO_OUTPUT_LOW);
      gpio_pin_set(gpio0_dev, 12, 0);
      sys_poweroff();
    }

    /* Power On success: clear manual power off flag */
    NRF_POWER->GPREGRET = 0;

    /* Configure Caps Lock LED (P0.31) as output high for sequencing */
    gpio_pin_configure(gpio0_dev, 31, GPIO_OUTPUT_HIGH);

    /* Sequential turn-on: BT, Green, Red, Mic Mute, Mute, Caps Lock */
    gpio_pin_set_raw(gpio1_dev, 2, 0); /* BT ON */
    k_busy_wait(150000);
    gpio_pin_set_raw(gpio1_dev, 4, 0); /* Battery Green ON */
    k_busy_wait(150000);
    gpio_pin_set_raw(gpio1_dev, 6, 0); /* Battery Red ON */
    k_busy_wait(150000);
    gpio_pin_set_raw(gpio1_dev, 7, 0); /* Mic Mute ON */
    k_busy_wait(150000);
    gpio_pin_set_raw(gpio1_dev, 15, 0); /* Mute ON */
    k_busy_wait(150000);
    gpio_pin_set_raw(gpio0_dev, 31, 0); /* Caps Lock ON */
    k_busy_wait(300000);

    /* Turn all OFF */
    gpio_pin_set_raw(gpio1_dev, 2, 1);
    gpio_pin_set_raw(gpio1_dev, 4, 1);
    gpio_pin_set_raw(gpio1_dev, 6, 1);
    gpio_pin_set_raw(gpio1_dev, 7, 1);
    gpio_pin_set_raw(gpio1_dev, 15, 1);
    gpio_pin_set_raw(gpio0_dev, 31, 1);
  }

  return 0;
}

SYS_INIT(board_gpio_init, PRE_KERNEL_2, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);