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
 *   P1.02  BT_LED     - Bluetooth status
 *   P1.06  BAT_LED_R  - Battery / charger Red
 *   P1.04  BAT_LED_G  - Battery / charger Green
 *   P1.03  CHG_INT    - Charger IC interrupt: LOW = charging, HIGH = done
 *                       (BMD-340)
 *   P0.29  Power LED  - Driven by PWM0 Channel 0 (breathing light effect)
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/logging/log.h>
#include <zmk/usb.h>

LOG_MODULE_REGISTER(status_leds, LOG_LEVEL_INF);

#include <soc.h>
#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>

#include "board_hw.h"

/* --------------------------------------------------------------------------
 * Device specs
 * -------------------------------------------------------------------------- */
static const struct device *gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
static const struct pwm_dt_spec pwm_led =
    PWM_DT_SPEC_GET(DT_NODELABEL(pwm_led_power));

/* Active-LOW LED helpers — set physical pin directly (no polarity abstraction)
 */
#define LED_ON(dev, pin)                                                       \
  gpio_pin_set(dev, pin, 0) /* physical LOW  — LED on                        \
                             */
#define LED_OFF(dev, pin)                                                      \
  gpio_pin_set(dev, pin, 1) /* physical HIGH — LED off */

/* --------------------------------------------------------------------------
 * Shared state updated by ZMK event listeners, read by the LED thread
 * -------------------------------------------------------------------------- */
static volatile bool bt_connected = false;
static volatile uint8_t battery_soc = 100; /* 0-100 %, default optimistic */
static volatile bool is_idle = false;

/* Pairing state machine (2026-08-20). Entered when BT layer 1 is active
 * (>= 1 key bound to `mo 1` is held - on this board both the Fn and the
 * ThinkVantage key activate layer 1) AND the power switch is held for
 * >= 2 s. layer1_active comes from ZMK's layer_state_changed event;
 * pwr_down is read via RAW GPIO (P1.11, like the 8 s power-off check).
 * 2026-08-20 (v24): REVERTED the per-key approach. V22/V23 listened to
 * specific positions/pins (pos 100, then P1.08) but those were guesses -
 * the physical ThinkVantage key's ZMK position was never confirmed, so the
 * narrowing broke the combo that provably worked in V21 (layer 1 + power
 * key). Going back to the layer-1 + raw power condition that worked, and
 * keeping the LATCHED mode: once active, the 4-LED blink + advertising
 * continue until a host connects (success) or 90 s timeout.
 *   IDLE -> (layer1 active AND pwr held >= 2 s) -> ACTIVE
 *   ACTIVE -> (bt_connected | 90 s timeout) -> IDLE */
static volatile bool layer1_active = false;  /* BT layer 1 (mo 1) active */
static volatile bool pairing_active = false; /* latched pairing mode */
static volatile int64_t pairing_arm_start =
    0; /* uptime when both conditions first held simultaneously */
static volatile int64_t pairing_deadline = 0; /* ACTIVE exit-by-timeout */

/* VBUS (USB power present) for the charging LED and the low-battery shutdown
 * guard. Event-driven: nRF52840 POWER peripheral USB-detect interrupts raise
 * USB_DC_CONNECTED/DISCONNECTED -> zmk_usb_conn_state_changed, so no polling
 * is needed (2026-08-17; the earlier 80 ms / 1 s / 2 min polling was removed
 * after tracing usb_dc_nrfx.c usb_dc_power_event_handler). Initial value is
 * read once at LED thread start; afterwards only the listener updates it. */
static volatile bool vbus_present = false;

/* --------------------------------------------------------------------------
 * LED worker thread
 * -------------------------------------------------------------------------- */
#define LED_THREAD_STACK_SIZE 1024
#define LED_THREAD_PRIORITY 7
/* Battery level shown for ~4.8 s after boot (10 ticks * 6 * 80 ms)
 * (review 2026-08-13 item 3.2). */
#define BOOT_DISPLAY_TICKS 10

/* Latched pairing mode timeout (2026-08-20): when ThinkVantage + power key
 * enters pairing mode, stay advertising/blinking until a host connects or
 * this many ms elapse, then return to normal. */
#define PAIRING_TIMEOUT_MS 90000

K_THREAD_STACK_DEFINE(led_stack, LED_THREAD_STACK_SIZE);
static struct k_thread led_thread_data;

static void led_thread_fn(void *a, void *b, void *c) {
  int toggle = 0;
  int blink_on = 0;
  int tick_count = 0;
  int breath_step = 0;
  int pwr_press_ticks = 0;
  bool pairing_prev = false; /* pairing falling-edge detector (restore LEDs) */

  /* Breathing duty cycle table (0 to 100) — 50 steps for a 4-second breathing
   * cycle */
  static const uint8_t breath_table[50] = {
      0,  2, 8, 18, 32, 50, 68, 82, 92, 98, 100, 98, 92, 82, 68, 50, 32,
      18, 8, 2, 0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,
      0,  0, 0, 0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0};

  while (1) {
    /* ---- Dump the ACTIVE clock sources once (2026-08-20, HW clock debug).
     * Reads the nRF52840 CLOCK status registers (HFCLKSTAT / LFCLKSTAT), not
     * Kconfig - so it reflects what is actually running. If an external
     * crystal is configured but not oscilating, running=0 (29): HFCLK SRC /\
     * LFCLK SRC comes from CLKSTAT.SRC: HF bit0 0=RC 1=XTAL ; LF bits0-1
     * 0=RC 1=XTAL 2=SYNTH. STATE running = bit16. ---- */
    static bool clock_dumped = false;
    if (!clock_dumped) {
      clock_dumped = true;
      uint32_t hf = NRF_CLOCK->HFCLKSTAT;
      uint32_t lf = NRF_CLOCK->LFCLKSTAT;
      LOG_INF("tpkb CLOCK: HFCLK [32M] src=%s running=%u raw=0x%04x | "
              "LFCLK [32.768k] src=%s running=%u raw=0x%04x",
              (hf & 0x01) ? "XTAL(ext)" : "RC(int)", (hf >> 16) & 0x1u,
              (unsigned)hf,
              ((lf & 0x03) == 0x01) ? "XTAL(ext)"
                                     : (((lf & 0x03) == 0x02) ? "SYNTH" : "RC(int)"),
              (lf >> 16) & 0x1u, (unsigned)lf);
    }

    /* ---- Manual Power-Off Button Check (Hold for 8 seconds) ---- */
    if (gpio_pin_get_raw(gpio1_dev, PWRSWITCH_PIN) ==
        0) { /* Pressed (active-LOW) */
      pwr_press_ticks++;
      if (pwr_press_ticks >= 100) { /* 100 * 80ms = 8000ms = 8 seconds */
        /* Set manual power off flag in GPREGRET */
        NRF_POWER->GPREGRET = MANUAL_POWER_OFF_FLAG;

        /* Turn all LEDs ON first (caps lock LED is owned by the indicator
         * driver and is left alone, review 2026-08-13 item 2.4) */
        gpio_pin_set_raw(gpio1_dev, BT_LED_PIN, 0);       /* BT LED ON */
        gpio_pin_set_raw(gpio1_dev, BAT_LED_G_PIN, 0);    /* Green ON */
        gpio_pin_set_raw(gpio1_dev, BAT_LED_R_PIN, 0);    /* Red ON */
        gpio_pin_set_raw(gpio1_dev, MIC_MUTE_LED_PIN, 0); /* Mic Mute ON */
        gpio_pin_set_raw(gpio1_dev, MUTE_LED_PIN, 0);     /* Mute ON */
        if (pwm_is_ready_dt(&pwm_led)) {
          pwm_set_pulse_dt(&pwm_led, pwm_led.period); /* Power LED ON */
        }
        k_msleep(300);

        /* Sequential turn-off: BT, Green, Red, Mic Mute, Mute */
        gpio_pin_set_raw(gpio1_dev, BT_LED_PIN, 1); /* BT OFF */
        k_msleep(150);
        gpio_pin_set_raw(gpio1_dev, BAT_LED_G_PIN, 1); /* Green OFF */
        k_msleep(150);
        gpio_pin_set_raw(gpio1_dev, BAT_LED_R_PIN, 1); /* Red OFF */
        k_msleep(150);
        gpio_pin_set_raw(gpio1_dev, MIC_MUTE_LED_PIN, 1); /* Mic Mute OFF */
        k_msleep(150);
        gpio_pin_set_raw(gpio1_dev, MUTE_LED_PIN, 1); /* Mute OFF */
        if (pwm_is_ready_dt(&pwm_led)) {
          pwm_set_pulse_dt(&pwm_led, 0); /* Power LED OFF */
        }
        k_msleep(150);

        /* 2026-08-19 fix (cannot power back on after 8 s shutdown):
         * the user is typically still holding the power key when the
         * shutdown animation ends, so GPIO DETECT (SENSE=LOW) is still
         * asserted at sys_poweroff() and the nRF52840 immediately wakes
         * back up (PS: "Setting the system to System OFF while DETECT is
         * high will cause a wakeup from System OFF reset"), racing the
         * 0xAA gate in board.c. Wait (max 1 s) for the key to be released
         * before entering System OFF. */
        for (int i = 0; i < 10; i++) {
          if (gpio_pin_get_raw(gpio1_dev, PWRSWITCH_PIN) ==
              1) { /* released */
            break;
          }
          k_msleep(100);
        }

        /* Cut off 5V Boost (P0.12) */
        gpio_pin_configure(gpio0_dev, BOOST_EN_PIN, GPIO_OUTPUT_LOW);
        gpio_pin_set(gpio0_dev, BOOST_EN_PIN, 0);

        /* Go to System OFF */
        sys_poweroff();
      }
    } else {
      pwr_press_ticks = 0;
    }

/* ---- Battery Critical Shutdown (<3.4V / <2% SoC) ---- */
/* Read the initial VBUS state once (event listener updates it afterwards). */
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    vbus_present = zmk_usb_is_powered();
#else
    vbus_present = false;
#endif

    if (battery_soc < 2 && !vbus_present) {
      /* Flash Red LED 5x rapidly */
      for (int i = 0; i < 5; i++) {
        LED_ON(gpio1_dev, BAT_LED_R_PIN);
        k_msleep(100);
        LED_OFF(gpio1_dev, BAT_LED_R_PIN);
        k_msleep(100);
      }

      /* Store the manual power-off flag so that a wake from any stray key
       * press in a bag cannot run the full wake -> low-battery -> shutdown
       * cycle again and drain the remaining charge (review 2026-08-13 item
       * 2.18). The next boot requires a deliberate 2-second power-button
       * hold; if USB power is present the board stays on after that. */
      NRF_POWER->GPREGRET = MANUAL_POWER_OFF_FLAG;

      /* Turn off 5V Boost (Trackpoint power) to prevent over-discharge */
      gpio_pin_configure(gpio0_dev, BOOST_EN_PIN, GPIO_OUTPUT_LOW);
      gpio_pin_set(gpio0_dev, BOOST_EN_PIN, 0);

      /* Turn off all LEDs and go to System OFF */
      LED_OFF(gpio1_dev, BT_LED_PIN);
      LED_OFF(gpio1_dev, BAT_LED_R_PIN);
      LED_OFF(gpio1_dev, BAT_LED_G_PIN);
      if (pwm_is_ready_dt(&pwm_led)) {
        pwm_set_pulse_dt(&pwm_led, 0); // Turn off power LED
      }
      sys_poweroff();
    }

    /* ---- Pairing state machine: BT layer 1 + power(P1.11) >= 2 s ----
     * layer1_active comes from ZMK events (any Fn/ThinkVantage key bound to
     * mo 1); pwr_down is a RAW P1.11 read (like the 8 s power-off check).
     * This reverts V22/V23 which listened to a single guessed key (pos 100 /
     * P1.08) and broke the combo that worked in V21. State: IDLE ->(both
     * held 2 s)-> ACTIVE (latched) until connect or 90 s timeout. */
    bool pwr_down = (gpio_pin_get_raw(gpio1_dev, PWRSWITCH_PIN) == 0); /* P1.11 power, active-low */

    if (!pairing_active) {
      if (layer1_active && pwr_down) {
        if (pairing_arm_start == 0) {
          pairing_arm_start = k_uptime_get();
        } else if (k_uptime_get() - pairing_arm_start >= 2000) {
          /* Enter latched pairing: clear bond + restart advertising - the
           * only reliable "pairable" trigger (zmk_ble_prof_select is a no-op
           * when the profile is already active). Blink continues after the
           * keys are released until connect or timeout. */
          LOG_INF("tpkb PAIRING: enter (layer1+pwr 2s), clearing bond + advertising");
          zmk_ble_clear_bonds();
          LOG_INF("tpkb PAIRING: bond cleared, advertising requested");
          pairing_active = true;
          pairing_deadline = k_uptime_get() + PAIRING_TIMEOUT_MS;
          pairing_arm_start = 0;
        }
      } else {
        pairing_arm_start = 0;
      }
    } else if (bt_connected || k_uptime_get() >= pairing_deadline) {
      /* Exit latched pairing on connect (success) or timeout. */
      LOG_INF("tpkb PAIRING: exit (%s)",
              bt_connected ? "connected" : "90s timeout");
      pairing_active = false;
    }

    /* Restore mute/mic on pairing falling edge (release / connect / timeout). */
    if (pairing_prev && !pairing_active) {
      LED_OFF(gpio1_dev, MUTE_LED_PIN);
      LED_OFF(gpio1_dev, MIC_MUTE_LED_PIN);
    }
    pairing_prev = pairing_active;

    /* ---- Pairing indication: BT + speaker-mute + mic-mute + power 4 LEDs
     *      blink together ~12.5 Hz while pairing mode is active ---- */
    if (pairing_active) {
      blink_on = !blink_on;
      if (blink_on) {
        LED_ON(gpio1_dev, BT_LED_PIN);
        LED_ON(gpio1_dev, MUTE_LED_PIN);
        LED_ON(gpio1_dev, MIC_MUTE_LED_PIN);
      } else {
        LED_OFF(gpio1_dev, BT_LED_PIN);
        LED_OFF(gpio1_dev, MUTE_LED_PIN);
        LED_OFF(gpio1_dev, MIC_MUTE_LED_PIN);
      }
      if (pwm_is_ready_dt(&pwm_led)) {
        pwm_set_pulse_dt(&pwm_led, blink_on ? pwm_led.period : 0);
        breath_step = 0; /* freeze breathing phase while pairing */
      }
    } else if (pwm_is_ready_dt(&pwm_led)) {
      /* Normal power LED: breathing. */
      uint32_t pulse = (pwm_led.period * breath_table[breath_step]) / 100;
      pwm_set_pulse_dt(&pwm_led, pulse);
      breath_step = (breath_step + 1) % 50;
    }

    /* ---- Status LEDs (BT & Battery) updated every 6 ticks (~480ms);
     *      skipped while pairing mode is active (4-LED fast blink takes over) ---- */
    if (tick_count % 6 == 0 && !pairing_active) {
      /* ---- BT LED ---- */
      if (is_idle) {
        LED_OFF(gpio1_dev, BT_LED_PIN);
      } else if (bt_connected) {
        LED_ON(gpio1_dev, BT_LED_PIN); /* solid ON when connected */
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
        int chg = gpio_pin_get(gpio1_dev, CHG_INT_PIN);
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
        if ((tick_count / 6) < BOOT_DISPLAY_TICKS && !is_idle) {
          if (battery_soc < 10) {             /* <3.5V is approx <10% SoC */
            LED_ON(gpio1_dev, BAT_LED_R_PIN); /* Low: Red  */
            LED_OFF(gpio1_dev, BAT_LED_G_PIN);
          } else {
            LED_OFF(gpio1_dev, BAT_LED_R_PIN);
            LED_ON(gpio1_dev, BAT_LED_G_PIN); /* OK:  Green */
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
 * Mute / mic-mute LEDs (local toggle)
 *
 * The standard HID LED report only carries Num / Caps / Scroll / Compose /
 * Kana - there is no mute bit - so the host-side mute state can never be
 * read back by the keyboard. We toggle the LED on key press instead.
 * Limitation: host-initiated mute changes (e.g. via the OS mixer) are not
 * reflected. Caps lock LED is NOT handled here: it uses the
 * zmk,indicator-leds host-report path and is driven by that driver.
 * Implemented 2026-08-17 per bring-up testing (positions 98/99 =
 * speaker-mute / mic-mute in the keymap).
 * -------------------------------------------------------------------------- */
static int mute_leds_position_listener(const zmk_event_t *eh) {
  const struct zmk_position_state_changed *ev =
      as_zmk_position_state_changed(eh);
  if (ev == NULL || !ev->state) {
    return ZMK_EV_EVENT_BUBBLE;
  }

  switch (ev->position) {
  case 98: /* speaker mute key (C_MUTE) -> -LED_MUTE */
    gpio_pin_toggle(gpio1_dev, MUTE_LED_PIN);
    break;
  case 99: /* mic-mute key (C_MUTE placeholder, no C_MIC_MUTE in ZMK)
              -> -LEDMICMUTE_R */
    gpio_pin_toggle(gpio1_dev, MIC_MUTE_LED_PIN);
    break;
  default:
    break;
  }

  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(status_leds_mute, mute_leds_position_listener);
ZMK_SUBSCRIPTION(status_leds_mute, zmk_position_state_changed);

/* BT layer 1 (any mo 1 key: Fn or ThinkVantage) activation state, used by
 * the pairing state machine with the raw power-key read. 2026-08-20 (v24):
 * restored from V21 - the per-key (pos 100 / P1.08) approach was guessed and
 * broke pairing; layer 1 is the condition that provably worked. */
static int layer_state_listener(const zmk_event_t *eh) {
  const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
  if (ev != NULL && ev->layer == 1) {
    layer1_active = ev->state;
  }
  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(status_leds_layer, layer_state_listener);
ZMK_SUBSCRIPTION(status_leds_layer, zmk_layer_state_changed);

/* VBUS power state - event-driven by the nRF52840 POWER USB-detect interrupt
 * (USB_DC_CONNECTED/DISCONNECTED -> zmk_usb_conn_state_changed). No polling. */
static int usb_conn_state_listener(const zmk_event_t *eh) {
  const struct zmk_usb_conn_state_changed *ev =
      as_zmk_usb_conn_state_changed(eh);
  if (ev != NULL) {
    vbus_present = (ev->conn_state != ZMK_USB_CONN_NONE);
  }
  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(status_leds_usb, usb_conn_state_listener);
ZMK_SUBSCRIPTION(status_leds_usb, zmk_usb_conn_state_changed);

/* --------------------------------------------------------------------------
 * ZMK Event Listeners
 *
 * These run in ZMK's event manager context - it is safe to call all ZMK APIs
 * here.  We just update the shared state flags and let the LED thread act on
 * them asynchronously.
 * -------------------------------------------------------------------------- */

/* BLE profile changed (connected / disconnected / profile switched) */
static int ble_profile_listener(const zmk_event_t *eh) {
  bt_connected = zmk_ble_active_profile_is_connected();
  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(status_leds_ble, ble_profile_listener);
ZMK_SUBSCRIPTION(status_leds_ble, zmk_ble_active_profile_changed);

/* Battery state changed */
static int battery_state_listener(const zmk_event_t *eh) {
  const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
  if (ev != NULL) {
    battery_soc = ev->state_of_charge;
  }
  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(status_leds_bat, battery_state_listener);
ZMK_SUBSCRIPTION(status_leds_bat, zmk_battery_state_changed);

/* Activity state changed listener */
static int activity_state_listener(const zmk_event_t *eh) {
  is_idle = (zmk_activity_get_state() == ZMK_ACTIVITY_IDLE ||
             zmk_activity_get_state() == ZMK_ACTIVITY_SLEEP);
  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(status_leds_activity, activity_state_listener);
ZMK_SUBSCRIPTION(status_leds_activity, zmk_activity_state_changed);

/* --------------------------------------------------------------------------
 * Module init: start the LED worker thread.
 * This runs at APPLICATION level, after ZMK subsystems are ready.
 * -------------------------------------------------------------------------- */
static int status_leds_init(void) {
  if (!device_is_ready(gpio0_dev) || !device_is_ready(gpio1_dev)) {
    return -ENODEV;
  }

  /* PWR switch (P1.11) and HOTKEY (P1.08) are deliberately NOT configured
   * here: both pins are owned by the direct kscan (zmk,kscan-gpio-direct in
   * thinkpad_wireless.dts, GPIO_ACTIVE_LOW | GPIO_PULL_UP). Calling
   * gpio_pin_configure() on a pin after the kscan armed its GPIOTE interrupt
   * makes gpio_nrfx REMOVE that trigger ("Remove previously configured
   * trigger when pin is reconfigured", zephyr/drivers/gpio/gpio_nrfx.c) - the
   * Fn/HOTKEY (and Power) key then never generates a scan event again (no
   * keymap log, no layer change). The RAW reads below (gpio_pin_get_raw)
   * work regardless of who configured the pin, so no reconfiguration is
   * needed (v0.2, 2026-08-21). */

  /* Mute / mic-mute LEDs: outputs, start OFF (active-LOW: 1 = off) */
  gpio_pin_configure(gpio1_dev, MUTE_LED_PIN, GPIO_OUTPUT);
  gpio_pin_set(gpio1_dev, MUTE_LED_PIN, 1);
  gpio_pin_configure(gpio1_dev, MIC_MUTE_LED_PIN, GPIO_OUTPUT);
  gpio_pin_set(gpio1_dev, MIC_MUTE_LED_PIN, 1);

  /* Seed state from current ZMK values */
  bt_connected = zmk_ble_active_profile_is_connected();
  battery_soc = zmk_battery_state_of_charge();
  is_idle = (zmk_activity_get_state() == ZMK_ACTIVITY_IDLE ||
             zmk_activity_get_state() == ZMK_ACTIVITY_SLEEP);

  k_thread_create(&led_thread_data, led_stack, K_THREAD_STACK_SIZEOF(led_stack),
                  led_thread_fn, NULL, NULL, NULL, LED_THREAD_PRIORITY, 0,
                  K_NO_WAIT);
  k_thread_name_set(&led_thread_data, "status_leds");
  return 0;
}

SYS_INIT(status_leds_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
