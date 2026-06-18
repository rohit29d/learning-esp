#pragma once

#include <stdbool.h>

/* ─── Touch event types ─────────────────────────────────── */
typedef enum {
  TOUCH_EVENT_NONE = 0,
  TOUCH_EVENT_SINGLE_TAP,  /* Touch1 single tap detected */
  TOUCH_EVENT_DOUBLE_TAP,  /* Touch1 double tap detected */
  TOUCH_EVENT_TRIPLE_TAP,  /* Touch1 triple tap detected */
  TOUCH_EVENT_HOLD,        /* Touch1 held >= 1 second */
  TOUCH_EVENT_T2_HOLD,     /* Touch2 held >= 2 seconds (mode toggle) */
  TOUCH_EVENT_DUAL_HOLD,   /* Both held >= 3 seconds (settings toggle) */
} touch_event_t;

/*
 * Initialize GPIO pins, ISR, and esp_timers for touch input.
 * Call once from app_main() before entering the main loop.
 *
 * GPIO 2 = Touch1,  GPIO 3 = Touch2  (active-high, no pull)
 */
void touch_driver_init(void);

/*
 * Non-blocking poll.  Returns the highest-priority pending event,
 * or TOUCH_EVENT_NONE if nothing happened.
 * Call every main-loop tick (~10 ms).
 */
touch_event_t touch_driver_poll(void);
