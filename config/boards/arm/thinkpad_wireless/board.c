/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * board.c — Hardware-level initialization only.
 * All LED application logic lives in status_leds.c which uses the
 * ZMK event manager and can safely call ZMK subsystem APIs.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

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
static int board_gpio_init(const struct device *dev)
{
    if (!device_is_ready(gpio0_dev) || !device_is_ready(gpio1_dev)) {
        return -ENODEV;
    }

    /* ---- LED outputs: initial state = OFF (physical HIGH, active-LOW hw) ---- */
    gpio_pin_configure(gpio1_dev,  2, GPIO_OUTPUT_HIGH); /* P1.02  BT LED          */
    gpio_pin_configure(gpio1_dev,  6, GPIO_OUTPUT_HIGH); /* P1.06  Battery Red LED  */
    gpio_pin_configure(gpio1_dev,  4, GPIO_OUTPUT_HIGH); /* P1.04  Battery Green LED*/
    gpio_pin_configure(gpio0_dev, 29, GPIO_OUTPUT_HIGH); /* P0.29  Power LED        */
    gpio_pin_configure(gpio1_dev, 15, GPIO_OUTPUT_HIGH); /* P1.15  Mute LED         */
    gpio_pin_configure(gpio1_dev,  7, GPIO_OUTPUT_HIGH); /* P1.07  Mic Mute LED     */

    /* ---- Input: charger interrupt (active LOW = charging) ---- */
    gpio_pin_configure(gpio0_dev,  8, GPIO_INPUT | GPIO_PULL_UP); /* P0.08 CHG_INT */

    return 0;
}

SYS_INIT(board_gpio_init, PRE_KERNEL_2, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);