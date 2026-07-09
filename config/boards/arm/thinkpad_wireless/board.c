/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/init.h>
#include <zephyr/pm/pm.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#define STACK_SIZE 1024
#define PRIORITY 7

K_THREAD_STACK_DEFINE(status_led_stack, STACK_SIZE);
struct k_thread status_led_thread_data;

bool is_bt_connected = false;

static void connected(struct bt_conn *conn, uint8_t err) {
    if (err == 0) {
        is_bt_connected = true;
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    is_bt_connected = false;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static int read_battery_millivolts(void) {
    const struct device *adc_dev = device_get_binding("ADC_0");
    if (!adc_dev) {
        return 3700;
    }
    
    struct adc_channel_cfg channel_cfg = {
        .gain = ADC_GAIN_1_6,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id = 0,
        .input_positive = 1, // AIN0 (P0.02)
    };
    
    adc_channel_setup(adc_dev, &channel_cfg);
    
    int16_t sample_buffer;
    struct adc_sequence sequence = {
        .channels = BIT(0),
        .buffer = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution = 12,
        .oversampling = 0,
        .calibrate = false,
    };
    
    int err = adc_read(adc_dev, &sequence);
    if (err < 0) {
        return 3700;
    }
    
    // Battery Voltage = raw * 7200 / 4095 mV (due to 50% voltage divider)
    int mv = (sample_buffer * 7200) / 4095;
    return mv;
}

void status_led_thread(void *dummy1, void *dummy2, void *dummy3) {
    const struct device *gpio0 = device_get_binding("GPIO_0");
    const struct device *gpio1 = device_get_binding("GPIO_1");
    
    if (!gpio0 || !gpio1) {
        return;
    }
    
    // Configure pins: LEDs are active LOW, so turn off (HIGH) initially
    gpio_pin_configure(gpio1, 2, GPIO_OUTPUT_ACTIVE | GPIO_OUTPUT_INIT_HIGH); // BT_LED (P1.02)
    gpio_pin_configure(gpio1, 6, GPIO_OUTPUT_ACTIVE | GPIO_OUTPUT_INIT_HIGH); // BAT_LED_R (P1.06)
    gpio_pin_configure(gpio1, 4, GPIO_OUTPUT_ACTIVE | GPIO_OUTPUT_INIT_HIGH); // BAT_LED_G (P1.04)
    gpio_pin_configure(gpio0, 8, GPIO_INPUT | GPIO_PULL_UP);                 // CHG_INT (P0.08)
    
    // 1. Initial Battery Check (Wakeup / Boot display)
    int initial_mv = read_battery_millivolts();
    
    // Under 3.4V: shutdown immediately
    if (initial_mv < 3400) {
        // Flash Red LED quickly 5 times (100ms ON, 100ms OFF)
        for (int i = 0; i < 5; i++) {
            gpio_pin_set(gpio1, 6, 0); // Red ON
            k_msleep(100);
            gpio_pin_set(gpio1, 6, 1); // Red OFF
            k_msleep(100);
        }
        // Force deep sleep (SOFT OFF)
        pm_state_force(0, &(struct pm_state_info){PM_STATE_SOFT_OFF, 0, 0});
        k_sleep(K_FOREVER);
    }
    
    // Wakeup show battery status for 5 seconds (10 loops of 500ms)
    // Low battery (< 3.5V) -> Red ON. Normal (>= 3.5V) -> Green ON.
    int show_battery_ticks = 10;
    if (initial_mv < 3500) {
        gpio_pin_set(gpio1, 6, 0); // Red ON
        gpio_pin_set(gpio1, 4, 1); // Green OFF
    } else {
        gpio_pin_set(gpio1, 6, 1); // Red OFF
        gpio_pin_set(gpio1, 4, 0); // Green ON
    }
    
    int toggle = 0;
    int tick_count = 0;
    
    while (1) {
        // Periodic battery voltage check (every 10 seconds / 20 loops)
        if (tick_count % 20 == 0) {
            int current_mv = read_battery_millivolts();
            if (current_mv < 3400) {
                // Low battery shutdown
                for (int i = 0; i < 5; i++) {
                    gpio_pin_set(gpio1, 6, 0); // Red ON
                    k_msleep(100);
                    gpio_pin_set(gpio1, 6, 1); // Red OFF
                    k_msleep(100);
                }
                pm_state_force(0, &(struct pm_state_info){PM_STATE_SOFT_OFF, 0, 0});
                k_sleep(K_FOREVER);
            }
        }
        
        // 1. Bluetooth LED logic
        if (is_bt_connected) {
            // Connected: solid ON (LOW)
            gpio_pin_set(gpio1, 2, 0);
        } else {
            // Pairing/Idle: flash (500ms on, 500ms off)
            gpio_pin_set(gpio1, 2, toggle ? 0 : 1);
        }
        
        // 2. Battery / Charger LED logic
        // Read VBUS status
        uint32_t usbreg = *((volatile uint32_t *)0x40000438);
        bool vbus_present = (usbreg & 0x1) != 0;
        
        if (vbus_present) {
            // USB connected: show charging state constantly
            int chg_int = gpio_pin_get(gpio0, 8);
            if (chg_int == 0) {
                // Charging: Red ON, Green OFF
                gpio_pin_set(gpio1, 6, 0);
                gpio_pin_set(gpio1, 4, 1);
            } else {
                // Charged: Red OFF, Green ON
                gpio_pin_set(gpio1, 6, 1);
                gpio_pin_set(gpio1, 4, 0);
            }
        } else {
            // Battery mode: show battery level briefly at wake-up, then turn off
            if (tick_count >= show_battery_ticks) {
                gpio_pin_set(gpio1, 6, 1); // Red OFF
                gpio_pin_set(gpio1, 4, 1); // Green OFF
            }
        }
        
        toggle = !toggle;
        tick_count++;
        k_msleep(500);
    }
}

int status_led_init(const struct device *dev) {
    k_thread_create(&status_led_thread_data, status_led_stack,
                    K_THREAD_STACK_SIZEOF(status_led_stack),
                    status_led_thread,
                    NULL, NULL, NULL,
                    PRIORITY, 0, K_NO_WAIT);
    return 0;
}

SYS_INIT(status_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
