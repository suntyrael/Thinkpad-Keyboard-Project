/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/pm/pm.h>
#include <hal/nrf_power.h>

#define STACK_SIZE 1024
#define PRIORITY 7

K_THREAD_STACK_DEFINE(status_led_stack, STACK_SIZE);
struct k_thread status_led_thread_data;

/* Use DT_NODELABEL to get device handles.
 * device_get_binding() was removed in Zephyr 4.x. */
static const struct device *gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

/* LED pin definitions (all GPIO_ACTIVE_LOW on hardware) */
#define BT_LED_PIN      2   /* P1.02 - Bluetooth status (gpio1) */
#define BAT_LED_R_PIN   6   /* P1.06 - Battery Red     (gpio1) */
#define BAT_LED_G_PIN   4   /* P1.04 - Battery Green   (gpio1) */
#define CHG_INT_PIN     8   /* P0.08 - Charger INT: LOW=charging (gpio0) */

/* LEDs are active-LOW: physical LOW = LED ON, physical HIGH = LED OFF.
 * We use GPIO_OUTPUT_HIGH at init so all LEDs start in OFF state.
 * gpio_pin_set() raw value: 0 = physical LOW = LED ON
 *                           1 = physical HIGH = LED OFF */
#define LED_ON(dev, pin)  gpio_pin_set(dev, pin, 0)
#define LED_OFF(dev, pin) gpio_pin_set(dev, pin, 1)

void status_led_thread(void *dummy1, void *dummy2, void *dummy3)
{
    if (!device_is_ready(gpio0_dev) || !device_is_ready(gpio1_dev)) {
        return;
    }

    /* Init LEDs: OUTPUT_HIGH = LED OFF for active-LOW hardware */
    gpio_pin_configure(gpio1_dev, BT_LED_PIN,    GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpio1_dev, BAT_LED_R_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpio1_dev, BAT_LED_G_PIN, GPIO_OUTPUT_HIGH);
    /* Charger interrupt: input with pull-up (LOW = charging, HIGH = done) */
    gpio_pin_configure(gpio0_dev, CHG_INT_PIN,   GPIO_INPUT | GPIO_PULL_UP);

    int toggle = 0;
    int tick_count = 0;

    while (1) {
        /* -------------------------------------------------------
         * BT LED: blink slowly when not connected, solid when connected.
         * We use a simple heuristic: after boot, always blink until
         * the system sets a connection. A ZMK event-based approach
         * would require ZMK event manager which is not available here.
         * For now, blink at 1 Hz to indicate advertising.
         * ------------------------------------------------------- */
        if (toggle) {
            LED_ON(gpio1_dev, BT_LED_PIN);
        } else {
            LED_OFF(gpio1_dev, BT_LED_PIN);
        }

        /* -------------------------------------------------------
         * Battery / Charger LED logic.
         * Use Nordic HAL to detect VBUS safely (no raw register access).
         * ------------------------------------------------------- */
        bool vbus_present = nrf_power_usbdetected_get(NRF_POWER);

        if (vbus_present) {
            /* CHG_INT is active-LOW: 0 = charging, 1 = fully charged */
            int chg_int = gpio_pin_get(gpio0_dev, CHG_INT_PIN);
            if (chg_int == 0) {
                /* Charging: Red ON, Green OFF */
                LED_ON(gpio1_dev, BAT_LED_R_PIN);
                LED_OFF(gpio1_dev, BAT_LED_G_PIN);
            } else {
                /* Charged/done: Red OFF, Green ON */
                LED_OFF(gpio1_dev, BAT_LED_R_PIN);
                LED_ON(gpio1_dev, BAT_LED_G_PIN);
            }
        } else {
            /* On battery: show for first 5 s (10 ticks), then off */
            if (tick_count < 10) {
                /* Green ON to indicate normal battery */
                LED_OFF(gpio1_dev, BAT_LED_R_PIN);
                LED_ON(gpio1_dev, BAT_LED_G_PIN);
            } else {
                LED_OFF(gpio1_dev, BAT_LED_R_PIN);
                LED_OFF(gpio1_dev, BAT_LED_G_PIN);
            }
        }

        toggle = !toggle;
        tick_count++;
        k_msleep(500);
    }
}

int status_led_init(const struct device *dev)
{
    k_thread_create(&status_led_thread_data, status_led_stack,
                    K_THREAD_STACK_SIZEOF(status_led_stack),
                    status_led_thread,
                    NULL, NULL, NULL,
                    PRIORITY, 0, K_NO_WAIT);
    return 0;
}

SYS_INIT(status_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);