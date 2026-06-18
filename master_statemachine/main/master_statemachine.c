#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// reactions headers
#include "Flash_eyes.h"
#include "angry_2.h"
#include "bruh.h"
#include "chomp.h"
#include "flash_eyes_1.h"
#include "game.h"
#include "happy.h"
#include "intro.h"
#include "love_1.h"
#include "music.h"
#include "pressed.h"
#include "proud.h"
#include "relaxed.h"
#include "reverse.h"
#include "sleep_to_wake.h"
#include "sleepy.h"
#include "sleepy_3.h"
#include "weep.h"

// drivers
#include "gif_player.h"
#include "i2c_oled.h"
#include "touch_driver.h"

static i2c_oled_t dev;

static const AnimatedGIF *idle_sequence[] = {
    &happy, &relaxed, &chomp, &game, &proud, &sleepy_3, &sleep_to_wake};
#define NUM_IDLE_GIFS (sizeof(idle_sequence) / sizeof(idle_sequence[0]))
static int current_idle_index = 0;

/* ─── ENUMS ─────────────────────────────────────────────── */
typedef enum { STATE_DEFAULT = 0, STATE_AI, STATE_SETTINGS } master_state_t;

/* ─── STATE CONTEXT ─────────────────────────────────────── */
typedef struct {
  master_state_t master;
} hsm_ctx_t;

static hsm_ctx_t ctx = {
    .master = STATE_DEFAULT,
};

/* ─── APP MAIN ──────────────────────────────────────────── */
void app_main(void) {
  printf("pebo HSM starting\n");

  // Initialize display device handle
  i2c_oled_init(&dev, 128, 64);
  i2c_oled_clear_screen(&dev, false);

  touch_driver_init();
  printf("touch driver initialized\n");

  // Start with idle animation in default state
  current_idle_index = 0;
  gif_set(idle_sequence[current_idle_index], 1);

  master_state_t last_state = -1;

  while (1) {
    // 1. Poll touch events (non-blocking)
    touch_event_t ev = touch_driver_poll();

    // 2. Handle master state transitions
    if (ev == TOUCH_EVENT_T2_HOLD) {
      if (ctx.master == STATE_DEFAULT) {
        ctx.master = STATE_AI;
      } else if (ctx.master == STATE_AI) {
        ctx.master = STATE_DEFAULT;
      }
      printf("→ TOGGLE to %s\n",
             ctx.master == STATE_AI ? "STATE_AI" : "STATE_DEFAULT");
    }

    if (ev == TOUCH_EVENT_DUAL_HOLD) {
      if (ctx.master != STATE_SETTINGS) {
        ctx.master = STATE_SETTINGS;
        printf("→ ENTER STATE_SETTINGS\n");
      } else {
        ctx.master = STATE_DEFAULT;
        printf("→ RETURN STATE_DEFAULT\n");
      }
    }

    // Handle screen clears or static text on state change
    if (ctx.master != last_state) {
      gif_stop();
      i2c_oled_clear_screen(&dev, false);
      if (ctx.master == STATE_DEFAULT) {
        // We handle GIF playback below, but can set it up here when entering
        current_idle_index = 0;
        gif_set(idle_sequence[current_idle_index], 1);
      } else if (ctx.master == STATE_AI) {
        i2c_oled_display_text(&dev, 2, "AI Mode", 7, false);
        i2c_oled_display_text(&dev, 4, "Coming soon...", 14, false);
      } else if (ctx.master == STATE_SETTINGS) {
        i2c_oled_display_text(&dev, 2, "Settings Mode", 13, false);
        i2c_oled_display_text(&dev, 4, "Coming soon...", 14, false);
      }
      last_state = ctx.master;
    }

    // 3. Handle state-specific logic (substates / reactions)
    if (ctx.master == STATE_DEFAULT) {
      if (ev == TOUCH_EVENT_SINGLE_TAP) {
        printf("SINGLE_TAP\n");
        gif_set(&flash_eyes_1, 1);
      } else if (ev == TOUCH_EVENT_DOUBLE_TAP) {
        printf("DOUBLE_TAP\n");
        gif_set(&angry_2, 1);
      } else if (ev == TOUCH_EVENT_TRIPLE_TAP) {
        printf("TRIPLE_TAP\n");
        gif_set(&weep, 1);
      } else if (ev == TOUCH_EVENT_HOLD) {
        printf("HOLD\n");
        gif_set(&love_1, 1);
      }

      // If one-shot animation finishes, return to idle
      if (!gif_is_playing()) {
        current_idle_index = (current_idle_index + 1) % NUM_IDLE_GIFS;
        gif_set(idle_sequence[current_idle_index], 1);
      }
    }

    // 4. Advance GIF frame (non-blocking)
    gif_tick();

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}