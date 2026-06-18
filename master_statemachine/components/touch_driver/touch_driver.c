/*
 * touch_driver.c — Non-blocking touch input driver
 *
 * Manages two capacitive/resistive touch buttons via GPIO ISR + esp_timer.
 * Detects single/double/triple taps, hold, Touch2-hold (mode toggle),
 * and dual-hold (settings toggle).  All detection is non-blocking;
 * touch_driver_poll() returns the next pending event without sleeping.
 */

#include "touch_driver.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ─── GPIO pins ──────────────────────────────────────────── */
#define TOUCH1 GPIO_NUM_2
#define TOUCH2 GPIO_NUM_3

/* ─── Timing constants (microseconds) ────────────────────── */
#define TAP_WINDOW_US    300000   /* 300 ms between taps */
#define HOLD_THRESHOLD_US 1000000 /* 1 sec for Touch1 hold */
#define T2_HOLD_US       2000000  /* 2 sec for Touch2 mode toggle */
#define DUAL_HOLD_US     3000000  /* 3 sec for both-held settings */
#define DEBOUNCE_US      50000    /* 50 ms debounce */

/* ─── ISR edge flags ─────────────────────────────────────── */
static volatile bool ev_t1_edge = false;
static volatile bool ev_t2_edge = false;

/* ─── Timer expired flags ────────────────────────────────── */
static volatile bool tap_window_expired = false;
static volatile bool hold_timer_expired = false;
static volatile bool t2_hold_expired    = false;
static volatile bool dual_hold_expired  = false;

/* ─── Timer handles ──────────────────────────────────────── */
static esp_timer_handle_t tap_timer;
static esp_timer_handle_t hold_timer;
static esp_timer_handle_t t2_hold_timer;
static esp_timer_handle_t dual_hold_timer;

/* ─── Tap state ──────────────────────────────────────────── */
static int  tap_count       = 0;
static bool t2_toggle_fired = false;
static bool dual_toggle_fired = false;

/* ─── Debounce state ─────────────────────────────────────── */
static int64_t last_t1_edge_us = 0;

/* ─── Pending event (consumed by poll) ───────────────────── */
static volatile touch_event_t pending_event = TOUCH_EVENT_NONE;

/* ─── Level helpers ──────────────────────────────────────── */
static inline bool t1_held(void)   { return gpio_get_level(TOUCH1) == 1; }
static inline bool t2_held(void)   { return gpio_get_level(TOUCH2) == 1; }
static inline bool both_held(void) { return t1_held() && t2_held(); }

/* ─── ISR ────────────────────────────────────────────────── */
static void IRAM_ATTR touch_isr_handler(void *arg) {
  uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
  if (gpio_num == TOUCH1)
    ev_t1_edge = true;
  else if (gpio_num == TOUCH2)
    ev_t2_edge = true;
}

/* ─── Timer callbacks ────────────────────────────────────── */
static void tap_timer_cb(void *a)       { tap_window_expired = true; }
static void hold_timer_cb(void *a)      { hold_timer_expired = true; }
static void t2_hold_timer_cb(void *a)   { t2_hold_expired    = true; }
static void dual_hold_timer_cb(void *a) { dual_hold_expired  = true; }

/* ─── Internal helpers ───────────────────────────────────── */
static void reset_tap_state(void) {
  tap_count          = 0;
  tap_window_expired = false;
  hold_timer_expired = false;
  esp_timer_stop(tap_timer);
  esp_timer_stop(hold_timer);
}

/* ═══════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════ */

void touch_driver_init(void) {
  /* GPIO configuration */
  gpio_config_t io_conf = {
      .pin_bit_mask  = (1ULL << TOUCH1) | (1ULL << TOUCH2),
      .mode          = GPIO_MODE_INPUT,
      .pull_up_en    = GPIO_PULLUP_DISABLE,
      .pull_down_en  = GPIO_PULLDOWN_DISABLE,
      .intr_type     = GPIO_INTR_POSEDGE,
  };
  gpio_config(&io_conf);

  /* ISR service */
  gpio_install_isr_service(0);
  gpio_isr_handler_add(TOUCH1, touch_isr_handler,
                       (void *)(uintptr_t)TOUCH1);
  gpio_isr_handler_add(TOUCH2, touch_isr_handler,
                       (void *)(uintptr_t)TOUCH2);

  /* Create timers */
  const esp_timer_create_args_t timer_args[] = {
      {.callback = tap_timer_cb,       .name = "tap_timer"},
      {.callback = hold_timer_cb,      .name = "hold_timer"},
      {.callback = t2_hold_timer_cb,   .name = "t2_hold"},
      {.callback = dual_hold_timer_cb, .name = "dual_hold"},
  };
  esp_timer_handle_t *handles[] = {
      &tap_timer, &hold_timer, &t2_hold_timer, &dual_hold_timer};
  for (int i = 0; i < 4; i++)
    esp_timer_create(&timer_args[i], handles[i]);
}

touch_event_t touch_driver_poll(void) {
  int64_t now_us = esp_timer_get_time();
  bool b1 = t1_held();
  bool b2 = t2_held();

  /* ── PRIORITY 1: Dual hold (both buttons) ─────────────── */
  if (b1 && b2) {
    /* Suppress single-button processing */
    ev_t1_edge = false;
    ev_t2_edge = false;
    reset_tap_state();

    /* Kill t2 toggle timer while both held */
    if (esp_timer_is_active(t2_hold_timer)) {
      esp_timer_stop(t2_hold_timer);
      t2_hold_expired = false;
      t2_toggle_fired = false;
    }

    if (!dual_toggle_fired) {
      if (dual_hold_expired) {
        dual_toggle_fired = true;
        return TOUCH_EVENT_DUAL_HOLD;
      } else if (!esp_timer_is_active(dual_hold_timer)) {
        dual_hold_expired = false;
        esp_timer_start_once(dual_hold_timer, DUAL_HOLD_US);
      }
    }
    return TOUCH_EVENT_NONE;
  }

  /* Both released — reset dual hold guard */
  if (!b1 || !b2) {
    if (esp_timer_is_active(dual_hold_timer))
      esp_timer_stop(dual_hold_timer);
    dual_hold_expired = false;
    if (!b1 && !b2)
      dual_toggle_fired = false;
  }

  /* ── PRIORITY 2: Touch2 hold (mode toggle) ────────────── */
  if (b2) {
    if (!t2_toggle_fired) {
      if (t2_hold_expired) {
        t2_toggle_fired = true;
        return TOUCH_EVENT_T2_HOLD;
      } else if (!esp_timer_is_active(t2_hold_timer)) {
        t2_hold_expired = false;
        esp_timer_start_once(t2_hold_timer, T2_HOLD_US);
      }
    }
  } else {
    if (esp_timer_is_active(t2_hold_timer))
      esp_timer_stop(t2_hold_timer);
    t2_hold_expired = false;
    t2_toggle_fired = false;
    ev_t2_edge      = false;
  }

  /* ── PRIORITY 3: Touch1 tap / hold detection ──────────── */

  /* Consume edge with timestamp-based debounce (no vTaskDelay) */
  bool local_t1 = false;
  if (ev_t1_edge) {
    if (now_us - last_t1_edge_us >= DEBOUNCE_US) {
      local_t1 = true;
      last_t1_edge_us = now_us;
    }
    ev_t1_edge = false;
  }

  /* Edge processing */
  if (local_t1) {
    if (tap_count == 0) {
      tap_count++;
      tap_window_expired = false;
      hold_timer_expired = false;
      esp_timer_start_once(tap_timer, TAP_WINDOW_US);
      esp_timer_start_once(hold_timer, HOLD_THRESHOLD_US);
    } else if (!tap_window_expired && tap_count == 1) {
      tap_count++;
      esp_timer_stop(tap_timer);
      tap_window_expired = false;
      esp_timer_start_once(tap_timer, TAP_WINDOW_US);
    } else if (!tap_window_expired && tap_count == 2) {
      tap_count++;
      reset_tap_state();
      return TOUCH_EVENT_TRIPLE_TAP;
    }
  }

  /* Tap-window resolution */
  if (tap_window_expired && tap_count > 0) {
    if (tap_count == 1 && !t1_held()) {
      reset_tap_state();
      return TOUCH_EVENT_SINGLE_TAP;
    } else if (tap_count == 2) {
      reset_tap_state();
      return TOUCH_EVENT_DOUBLE_TAP;
    }
  }

  /* Hold resolution */
  if (hold_timer_expired && tap_count == 1 && t1_held()) {
    reset_tap_state();
    return TOUCH_EVENT_HOLD;
  }

  return TOUCH_EVENT_NONE;
}
