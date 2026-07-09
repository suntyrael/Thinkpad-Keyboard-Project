/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
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
    
    int toggle = 0;
    
    while (1) {
        // 1. Bluetooth LED logic
        if (is_bt_connected) {
            // Connected: solid ON (LOW)
            gpio_pin_set(gpio1, 2, 0);
        } else {
            // Pairing/Idle: flash (500ms on, 500ms off)
            gpio_pin_set(gpio1, 2, toggle ? 0 : 1);
        }
        
        // 2. Battery and Charger LEDs logic
        // Read VBUS status from nRF52840 register USBREGSTATUS
        uint32_t usbreg = *((volatile uint32_t *)0x40000438);
        bool vbus_present = (usbreg & 0x1) != 0;
        
        if (vbus_present) {
            // USB plugged in: Read CHG_INT
            int chg_int = gpio_pin_get(gpio0, 8);
            if (chg_int == 0) {
                // Charging: Red ON (LOW), Green OFF (HIGH)
                gpio_pin_set(gpio1, 6, 0);
                gpio_pin_set(gpio1, 4, 1);
            } else {
                // Charged: Red OFF (HIGH), Green ON (LOW)
                gpio_pin_set(gpio1, 6, 1);
                gpio_pin_set(gpio1, 4, 0);
            }
        } else {
            // Battery mode: turn OFF both LEDs to conserve battery
            gpio_pin_set(gpio1, 6, 1);
            gpio_pin_set(gpio1, 4, 1);
        }
        
        toggle = !toggle;
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
