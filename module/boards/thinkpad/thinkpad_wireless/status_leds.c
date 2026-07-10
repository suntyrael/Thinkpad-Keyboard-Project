/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * status_leds.c — Application-level LED control using the ZMK event manager.
 *
 * This file subscribes to ZMK events (BLE profile change, battery state change)
 * and drives the status LEDs accordingly.  It must NOT use SYS_INIT because
 * the ZMK subsystems (BLE, battery) are not ready at that stage.
 * The ZMK event manager guarantees that listeners are called after full init.
 *
 * LED hardware (all active-LOW):
 *   P1.02  BT_LED        — Bluetooth status
 *   P1.06  BAT_LED_R     — Battery / charger Red
 *   P1.04  BAT_LED_G     — Battery / charger Green
 *   P0.08  CHG_INT       — Charger IC interrupt: LOW = charging, HIGH = done
 *   P0.29  Power LED     — Driven by PWM0 Channel 0 (breathing light effect)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/poweroff.h>
#include <zmk/usb.h>

#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/ble.h>
#include <zmk/battery.h>

/* --------------------------------------------------------------------------
 * Device specs
 * -------------------------------------------------------------------------- */
static const struct device *gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_led_power));

/* --------------------------------------------------------------------------
 * Pin definitions
 * -------------------------------------------------------------------------- */
#define BT_LED_PIN      2   /* P1.02  gpio1 */
#define BAT_LED_R_PIN   6   /* P1.06  gpio1 */
#define BAT_LED_G_PIN   4   /* P1.04  gpio1 */
#define CHG_INT_PIN     8   /* P0.08  gpio0 */

/* Active-LOW LED helpers — set physical pin directly (no polarity abstraction) */
#define LED_ON(dev, pin)  gpio_pin_set(dev, pin, 0)   /* physical LOW  — LED on  */
#define LED_OFF(dev, pin) gpio_pin_set(dev, pin, 1)   /* physical HIGH — LED off */

/* --------------------------------------------------------------------------
 * Shared state updated by ZMK event listeners, read by the LED thread
 * -------------------------------------------------------------------------- */
static volatile bool bt_connected  = false;
static volatile uint8_t battery_soc = 100; /* 0-100 %, default optimistic */

/* --------------------------------------------------------------------------
 * LED worker thread
 * -------------------------------------------------------------------------- */
#define LED_THREAD_STACK_SIZE 512
#define LED_THREAD_PRIORITY   7
#define BOOT_DISPLAY_TICKS    10    /* show battery level for 5 s after boot */

K_THREAD_STACK_DEFINE(led_stack, LED_THREAD_STACK_SIZE);
static struct k_thread led_thread_data;

static void led_thread_fn(void *a, void *b, void *c)
{
    int toggle      = 0;
    int tick_count  = 0;
    int breath_step = 0;

    /* Breathing duty cycle table (0 to 100) — 50 steps for a 4-second breathing cycle */
    static const uint8_t breath_table[50] = {
        0, 2, 8, 18, 32, 50, 68, 82, 92, 98, 100, 98, 92, 82, 68, 50, 32, 18, 8, 2,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    while (1) {
        /* ---- Battery Critical Shutdown (<3.4V / <2% SoC) ---- */
        /* Use Nordic HAL for VBUS detection (safe, no raw register access) */
        #if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        bool vbus_present = zmk_usb_is_powered();
#else
        bool vbus_present = false;
#endif

        if (battery_soc < 2 && !vbus_present) {
            /* Flash Red LED 5x rapidly */
            for (int i = 0; i < 5; i++) {
                LED_ON(gpio1_dev, BAT_LED_R_PIN);
                k_msleep(100);
                LED_OFF(gpio1_dev, BAT_LED_R_PIN);
                k_msleep(100);
            }
            /* Turn off all LEDs and go to System OFF */
            LED_OFF(gpio1_dev, BT_LED_PIN);
            LED_OFF(gpio1_dev, BAT_LED_R_PIN);
            LED_OFF(gpio1_dev, BAT_LED_G_PIN);
            if (pwm_is_ready_dt(&pwm_led)) {
                pwm_set_pulse_dt(&pwm_led, 0); // Turn off power LED
            }
            sys_poweroff();
        }

        /* ---- Power LED PWM Breathing ---- */
        if (pwm_is_ready_dt(&pwm_led)) {
            uint32_t period = pwm_led.period;
            uint32_t pulse = (period * breath_table[breath_step]) / 100;
            pwm_set_pulse_dt(&pwm_led, pulse);
        }
        breath_step = (breath_step + 1) % 50;

        /* ---- Status LEDs (BT & Battery) updated every 6 ticks (~480ms) ---- */
        if (tick_count % 6 == 0) {
            /* ---- BT LED ---- */
            if (bt_connected) {
                LED_ON(gpio1_dev, BT_LED_PIN);        /* solid ON when connected */
            } else {
                /* blink ~1 Hz while advertising / idle */
                if (toggle) {
                    LED_ON(gpio1_dev, BT_LED_PIN);
                } else {
                    LED_OFF(gpio1_dev, BT_LED_PIN);
                }
            }

            /* ---- Battery / Charger LED ---- */
            if (vbus_present) {
                /* USB plugged in: show charger IC status via CHG_INT pin */
                int chg = gpio_pin_get(gpio0_dev, CHG_INT_PIN);
                if (chg == 0) {
                    /* Charging: Red ON, Green OFF */
                    LED_ON(gpio1_dev, BAT_LED_R_PIN);
                    LED_OFF(gpio1_dev, BAT_LED_G_PIN);
                } else {
                    /* Fully charged: Red OFF, Green ON */
                    LED_OFF(gpio1_dev, BAT_LED_R_PIN);
                    LED_ON(gpio1_dev, BAT_LED_G_PIN);
                }
            } else {
                /* On battery: show level for BOOT_DISPLAY_TICKS then off */
                if ((tick_count / 6) < BOOT_DISPLAY_TICKS) {
                    if (battery_soc < 10) { /* <3.5V is approx <10% SoC */
                        LED_ON(gpio1_dev, BAT_LED_R_PIN);  /* Low: Red  */
                        LED_OFF(gpio1_dev, BAT_LED_G_PIN);
                    } else {
                        LED_OFF(gpio1_dev, BAT_LED_R_PIN);
                        LED_ON(gpio1_dev, BAT_LED_G_PIN);  /* OK:  Green */
                    }
                } else {
                    /* LEDs off to save power */
                    LED_OFF(gpio1_dev, BAT_LED_R_PIN);
                    LED_OFF(gpio1_dev, BAT_LED_G_PIN);
                }
            }

            toggle = !toggle;
        }

        tick_count++;
        k_msleep(80);
    }
}

/* --------------------------------------------------------------------------
 * ZMK Event Listeners
 *
 * These run in ZMK's event manager context — it is safe to call all ZMK APIs
 * here.  We just update the shared state flags and let the LED thread act on
 * them asynchronously.
 * -------------------------------------------------------------------------- */

/* BLE profile changed (connected / disconnected / profile switched) */
static int ble_profile_listener(const zmk_event_t *eh)
{
    bt_connected = zmk_ble_active_profile_is_connected();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(status_leds_ble, ble_profile_listener);
ZMK_SUBSCRIPTION(status_leds_ble, zmk_ble_active_profile_changed);

/* Battery state changed */
static int battery_state_listener(const zmk_event_t *eh)
{
    const struct zmk_battery_state_changed *ev =
        as_zmk_battery_state_changed(eh);
    if (ev != NULL) {
        battery_soc = ev->state_of_charge;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(status_leds_bat, battery_state_listener);
ZMK_SUBSCRIPTION(status_leds_bat, zmk_battery_state_changed);

/* --------------------------------------------------------------------------
 * Module init: start the LED worker thread.
 * This runs at APPLICATION level, after ZMK subsystems are ready.
 * -------------------------------------------------------------------------- */
static int status_leds_init(void)
{
    if (!device_is_ready(gpio0_dev) || !device_is_ready(gpio1_dev)) {
        return -ENODEV;
    }

    /* Seed state from current ZMK values */
    bt_connected  = zmk_ble_active_profile_is_connected();
    battery_soc   = zmk_battery_state_of_charge();

    k_thread_create(&led_thread_data, led_stack,
                    K_THREAD_STACK_SIZEOF(led_stack),
                    led_thread_fn, NULL, NULL, NULL,
                    LED_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&led_thread_data, "status_leds");
    return 0;
}

SYS_INIT(status_leds_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
