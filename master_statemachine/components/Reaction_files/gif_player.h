#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  const uint8_t *const *frames;
  const uint16_t *delays;
  uint8_t frame_count;
  uint16_t width;
  uint16_t height;
} AnimatedGIF;

/* ─── Blocking API (legacy) ──────────────────────────────── */
void playGIF(const AnimatedGIF *gif, uint8_t loop_count);

/* ─── Non-blocking API ───────────────────────────────────── */

/*
 * Set the animation to play.
 *   loop_count = 0  →  loop forever
 *   loop_count = N  →  play N times then stop
 * Resets the frame index to 0 and starts immediately on next gif_tick().
 */
void gif_set(const AnimatedGIF *gif, uint8_t loop_count);

/*
 * Advance one frame if the current frame's delay has elapsed.
 * Call this every main-loop tick (~10 ms).  Never blocks.
 */
void gif_tick(void);

/* Stop the current animation.  Screen retains last frame. */
void gif_stop(void);

/* Returns true if a non-blocking animation is currently active. */
bool gif_is_playing(void);
