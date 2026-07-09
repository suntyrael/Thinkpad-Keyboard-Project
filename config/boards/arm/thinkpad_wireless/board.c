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
#include <zmk/battery.h>

#define STACK_SIZE 1024
#define PRIORITY 7

K_THREAD_STACK_DEFINE(status_led_stack, STACK_SIZE);
struct k_thread status_led_thread_data;

/* Use DT_NODELABEL to get device handles - device_get_binding() is removed in Zephyr 4.x */
static const struct device *gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

/* LED pin definitions (all GPIO_ACTIVE_LOW on hardware) */
#define BT_LED_PORT     gpio1_dev
#define BT_LED_PIN      2   /* P1.02 - Bluetooth status */
#define BAT_LED_R_PORT  gpio1_dev
#define BAT_LED_R_PIN   6   /* P1.06 - Battery Red */
#define BAT_LED_G_PORT  gpio1_dev
#define BAT_LED_G_PIN   4   /* P1.04 - Battery Green */
#define CHG_INT_PORT    gpio0_dev
#define CHG_INT_PIN     8   /* P0.08 - Charger interrupt (active LOW when charging) */

/* LED helpers: LEDs are active-LOW, so "on" = gpio set 0, "off" = gpio set 1 */
#define LED_ON(port, pin)  gpio_pin_set(port, pin, 0)
#define LED_OFF(port, pin) gpio_pin_set(port, pin, 1)

void status_led_thread(void *dummy1, void *dummy2, void *dummy3)
{
    /* Validate device readiness */
    if (!device_is_ready(gpio0_dev) || !device_is_ready(gpio1_dev)) {
        return;
    }

    /* Configure LED output pins as OUTPUT_HIGH.
     * Our LEDs are active-LOW (hardware), so physical HIGH = LED OFF at init.
     * We do NOT pass GPIO_ACTIVE_LOW here because LED_ON/LED_OFF macros use raw
     * physical values (0=LOW=ON, 1=HIGH=OFF) without polarity abstraction. */
    gpio_pin_configure(BT_LED_PORT,    BT_LED_PIN,    GPIO_OUTPUT_HIGH);   /* HIGH = LED OFF (active-LOW hw) */
    gpio_pin_configure(BAT_LED_R_PORT, BAT_LED_R_PIN, GPIO_OUTPUT_HIGH);   /* HIGH = LED OFF (active-LOW hw) */
    gpio_pin_configure(BAT_LED_G_PORT, BAT_LED_G_PIN, GPIO_OUTPUT_HIGH);   /* HIGH = LED OFF (active-LOW hw) */

    /* Configure charger interrupt as input with pull-up */
    gpio_pin_configure(CHG_INT_PORT, CHG_INT_PIN, GPIO_INPUT | GPIO_PULL_UP);

    /* -----------------------------------------------------------------------
     * Initial battery check using ZMK's battery API (avoids ADC contention
     * with ZMK's own zmk,battery-voltage-divider driver).
     * zmk_battery_state_of_charge() returns 0-100 (%).
     * ----------------------------------------------------------------------- */
    uint8_t soc = zmk_battery_state_of_charge();

    /* Under ~5% SoC (~3.4V): warn and force SOFT_OFF */
    if (soc < 5) {
        /* Flash Red LED 5x fast to signal critical battery */
        for (int i = 0; i < 5; i++) {
            LED_ON(BAT_LED_R_PORT, BAT_LED_R_PIN);
            k_msleep(100);
            LED_OFF(BAT_LED_R_PORT, BAT_LED_R_PIN);
            k_msleep(100);
        }
        pm_state_force(0u, &(struct pm_state_info){PM_STATE_SOFT_OFF, 0, 0});
        k_sleep(K_FOREVER);
    }

    /* Show battery level briefly on boot (5 s = 10 x 500 ms ticks) */
    const int BOOT_DISPLAY_TICKS = 10;
    if (soc < 20) {
        LED_ON(BAT_LED_R_PORT, BAT_LED_R_PIN);   /* Low: Red */
        LED_OFF(BAT_LED_G_PORT, BAT_LED_G_PIN);
    } else {
        LED_OFF(BAT_LED_R_PORT, BAT_LED_R_PIN);  /* Normal: Green */
        LED_ON(BAT_LED_G_PORT, BAT_LED_G_PIN);
    }

    int toggle = 0;
    int tick_count = 0;

    while (1) {
        /* -------------------------------------------------------
         * Periodic battery check every 10 s (20 x 500 ms ticks).
         * Use ZMK SoC value - no direct ADC access needed.
         * ------------------------------------------------------- */
        if (tick_count % 20 == 0) {
            soc = zmk_battery_state_of_charge();
            if (soc < 5) {
                for (int i = 0; i < 5; i++) {
                    LED_ON(BAT_LED_R_PORT, BAT_LED_R_PIN);
                    k_msleep(100);
                    LED_OFF(BAT_LED_R_PORT, BAT_LED_R_PIN);
                    k_msleep(100);
                }
                pm_state_force(0u, &(struct pm_state_info){PM_STATE_SOFT_OFF, 0, 0});
                k_sleep(K_FOREVER);
            }
        }

        /* -------------------------------------------------------
         * Bluetooth LED: solid when connected, blinking when not.
         * ZMK manages BLE internally; we query its connection state
         * via zmk_ble_active_profile_is_connected().
         * ------------------------------------------------------- */
        extern bool zmk_ble_active_profile_is_connected(void);
        if (zmk_ble_active_profile_is_connected()) {
            LED_ON(BT_LED_PORT, BT_LED_PIN);   /* Connected: solid */
        } else {
            /* Not connected: blink 500 ms period */
            if (toggle) {
                LED_ON(BT_LED_PORT, BT_LED_PIN);
            } else {
                LED_OFF(BT_LED_PORT, BT_LED_PIN);
            }
        }

        /* -------------------------------------------------------
         * Battery / Charger LED logic.
         * Use the Nordic HAL to read VBUS status safely instead of
         * a raw register address.
         * ------------------------------------------------------- */
        bool vbus_present = nrf_power_usbdetected_get(NRF_POWER);

        if (vbus_present) {
            /* CHG_INT is active-LOW: 0 = charging, 1 = charged/done */
            int chg_int = gpio_pin_get(CHG_INT_PORT, CHG_INT_PIN);
            if (chg_int == 0) {
                /* Charging: Red ON, Green OFF */
                LED_ON(BAT_LED_R_PORT, BAT_LED_R_PIN);
                LED_OFF(BAT_LED_G_PORT, BAT_LED_G_PIN);
            } else {
                /* Charged: Red OFF, Green ON */
                LED_OFF(BAT_LED_R_PORT, BAT_LED_R_PIN);
                LED_ON(BAT_LED_G_PORT, BAT_LED_G_PIN);
            }
        } else {
            /* On battery: show level briefly at boot, then LEDs off */
            if (tick_count >= BOOT_DISPLAY_TICKS) {
                LED_OFF(BAT_LED_R_PORT, BAT_LED_R_PIN);
                LED_OFF(BAT_LED_G_PORT, BAT_LED_G_PIN);
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