#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

/* ─── GPIO ─────────────────────────────────────────────── */
#define TOUCH1  GPIO_NUM_2
#define TOUCH2  GPIO_NUM_3

/* ─── ENUMS ─────────────────────────────────────────────── */
typedef enum {
    STATE_DEFAULT = 0,
    STATE_AI,
    STATE_SETTINGS
} master_state_t;

typedef enum {
    SUB_DEFAULT_IDLE = 0,
    SUB_DEFAULT_SINGLE_TAP,
    SUB_DEFAULT_DOUBLE_TAP,
    SUB_DEFAULT_TRIPLE_TAP,
    SUB_DEFAULT_HOLD
} default_substate_t;

typedef enum {
    SUB_AI_IDLE = 0          /* placeholder */
} ai_substate_t;

typedef enum {
    SUB_SETTINGS_IDLE = 0    /* placeholder */
} settings_substate_t;

/* ─── STATE CONTEXT ─────────────────────────────────────── */
typedef struct {
    master_state_t  master;
    default_substate_t  sub_default;
    ai_substate_t       sub_ai;
    settings_substate_t sub_settings;
} hsm_ctx_t;

static hsm_ctx_t ctx = {
    .master      = STATE_DEFAULT,
    .sub_default = SUB_DEFAULT_IDLE,
    .sub_ai      = SUB_AI_IDLE,
    .sub_settings= SUB_SETTINGS_IDLE,
};

/* ─── RAW EDGE FLAGS (ISR sets, main loop clears) ───────── */
static volatile bool ev_t1_edge = false;
static volatile bool ev_t2_edge = false;

/* ─── TIMER EXPIRED FLAGS ───────────────────────────────── */
static volatile bool tap_window_expired  = false;
static volatile bool hold_timer_expired  = false;
static volatile bool t2_hold_expired     = false;   /* 2-sec toggle */
static volatile bool dual_hold_expired   = false;   /* 3-sec settings */

/* ─── TIMER HANDLES ─────────────────────────────────────── */
static esp_timer_handle_t tap_timer;
static esp_timer_handle_t hold_timer;
static esp_timer_handle_t t2_hold_timer;
static esp_timer_handle_t dual_hold_timer;

/* ─── TAP COUNTER ───────────────────────────────────────── */
static int tap_count = 0;

/* ─── ONE-SHOT GUARDS ───────────────────────────────────── */
static bool t2_toggle_fired    = false;   /* prevent re-fire while held */
static bool dual_toggle_fired  = false;

/* ─── ISR ───────────────────────────────────────────────── */
void IRAM_ATTR touch_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    if (gpio_num == TOUCH1) ev_t1_edge = true;
    else if (gpio_num == TOUCH2) ev_t2_edge = true;
}

/* ─── TIMER CALLBACKS (flags only) ─────────────────────── */
static void tap_timer_cb(void *a)        { tap_window_expired = true; }
static void hold_timer_cb(void *a)       { hold_timer_expired = true; }
static void t2_hold_timer_cb(void *a)    { t2_hold_expired    = true; }
static void dual_hold_timer_cb(void *a)  { dual_hold_expired  = true; }

/* ─── HELPERS ───────────────────────────────────────────── */
static inline bool t1_held(void) { return gpio_get_level(TOUCH1) == 1; }
static inline bool t2_held(void) { return gpio_get_level(TOUCH2) == 1; }
static inline bool both_held(void) { return t1_held() && t2_held(); }

static void reset_tap_state(void)
{
    tap_count           = 0;
    tap_window_expired  = false;
    hold_timer_expired  = false;
    esp_timer_stop(tap_timer);
    esp_timer_stop(hold_timer);
}

static void reset_master_timers(void)
{
    t2_hold_expired    = false;
    dual_hold_expired  = false;
    t2_toggle_fired    = false;
    dual_toggle_fired  = false;
    esp_timer_stop(t2_hold_timer);
    esp_timer_stop(dual_hold_timer);
}

/* ─── SUBSTATE HANDLERS ─────────────────────────────────── */

static void handle_default_substates(void)
{
    bool local_t1 = ev_t1_edge;
    if (local_t1) ev_t1_edge = false;

    /* debounce */
    if (local_t1) vTaskDelay(pdMS_TO_TICKS(50));

    /* touch1 edge processing */
    if (local_t1) {
        if (tap_count == 0) {
            tap_count++;
            tap_window_expired = false;
            hold_timer_expired = false;
            esp_timer_start_once(tap_timer,  300000);
            esp_timer_start_once(hold_timer, 1000000);
        } else if (!tap_window_expired && tap_count == 1) {
            tap_count++;
            esp_timer_stop(tap_timer);
            tap_window_expired = false;
            esp_timer_start_once(tap_timer, 300000);
        } else if (!tap_window_expired && tap_count == 2) {
            tap_count++;
            ctx.sub_default = SUB_DEFAULT_TRIPLE_TAP;
            printf("TRIPLE_TAP → state1:substate3\n");
            reset_tap_state();
            vTaskDelay(pdMS_TO_TICKS(3000));
            ctx.sub_default = SUB_DEFAULT_IDLE;
            return;
        }
    }

    /* tap-window resolution */
    if (tap_window_expired && tap_count > 0) {
        if (tap_count == 1 && !t1_held()) {
            ctx.sub_default = SUB_DEFAULT_SINGLE_TAP;
            printf("SINGLE_TAP → state1:substate1\n");
            reset_tap_state();
            ctx.sub_default = SUB_DEFAULT_IDLE;
        } else if (tap_count == 2) {
            ctx.sub_default = SUB_DEFAULT_DOUBLE_TAP;
            printf("DOUBLE_TAP → state1:substate2\n");
            reset_tap_state();
            ctx.sub_default = SUB_DEFAULT_IDLE;
        }
    }

    /* hold resolution */
    if (hold_timer_expired && tap_count == 1 && t1_held()) {
        ctx.sub_default = SUB_DEFAULT_HOLD;
        printf("HOLD\n");
        reset_tap_state();
        ctx.sub_default = SUB_DEFAULT_IDLE;
    }
}

static void handle_ai_substates(void)
{
    /* placeholder – add AI substates here */
    (void)0;
}

static void handle_settings_substates(void)
{
    /* placeholder – add Settings substates here */
    (void)0;
}

/* ─── STATE HANDLERS ────────────────────────────────────── */

static void handle_default_state(void)
{
    static int64_t next_log_us = 0;
    int64_t now = esp_timer_get_time();
    if (now >= next_log_us) {
        printf("-- STATE_DEFAULT --\n");
        next_log_us = now + 1000000;
    }
    handle_default_substates();
}

static void handle_ai_state(void)
{
    printf("-- STATE_AI --\n");
    handle_ai_substates();
}

static void handle_settings_state(void)
{
    printf("-- STATE_SETTINGS --\n");
    handle_settings_substates();
}

/* ─── MASTER STATE MACHINE ──────────────────────────────── */

static void process_master_state(void)
{
    bool b1 = t1_held();
    bool b2 = t2_held();

    /* ── PRIORITY: Settings entry/exit ───────────────────── */
    if (b1 && b2) {
        /* suppress both edges and kill any in-progress tap gesture */
        ev_t1_edge = false;
        ev_t2_edge = false;
        reset_tap_state();

        /* stop t2 toggle timer — not relevant while both held */
        if (esp_timer_is_active(t2_hold_timer)) {
            esp_timer_stop(t2_hold_timer);
            t2_hold_expired = false;
            t2_toggle_fired = false;
        }

        if (!dual_toggle_fired) {
            if (dual_hold_expired) {
                if (ctx.master != STATE_SETTINGS) {
                    ctx.master = STATE_SETTINGS;
                    printf("→ ENTER STATE_SETTINGS\n");
                } else {
                    printf("SETTINGS: save & exit\n");
                    ctx.master = STATE_DEFAULT;
                    printf("→ RETURN STATE_DEFAULT\n");
                }
                dual_toggle_fired = true;
            } else if (!esp_timer_is_active(dual_hold_timer)) {
                dual_hold_expired = false;
                esp_timer_start_once(dual_hold_timer, 3000000);
            }
        }

        return; /* skip all state dispatch while both held */
    }

    /* both released — reset dual hold guard */
    if (!b1 || !b2) {
        if (esp_timer_is_active(dual_hold_timer))
            esp_timer_stop(dual_hold_timer);
        dual_hold_expired = false;
        if (!b1 && !b2) {
            dual_toggle_fired = false;
        }
    }
    
    /* ── touch2 mode toggle (DEFAULT ↔ AI) ───────────────── */
    if (ctx.master == STATE_DEFAULT || ctx.master == STATE_AI) {
        if (b2) {
            if (!t2_toggle_fired) {
                if (t2_hold_expired) {
                    ctx.master = (ctx.master == STATE_DEFAULT)
                                 ? STATE_AI : STATE_DEFAULT;
                    printf("→ TOGGLE to %s\n",
                           ctx.master == STATE_AI ? "STATE_AI" : "STATE_DEFAULT");
                    t2_toggle_fired = true;
                } else if (!esp_timer_is_active(t2_hold_timer)) {
                    t2_hold_expired = false;
                    esp_timer_start_once(t2_hold_timer, 2000000);
                }
            }
        } else {
            if (esp_timer_is_active(t2_hold_timer))
                esp_timer_stop(t2_hold_timer);
            t2_hold_expired = false;
            t2_toggle_fired = false;
            ev_t2_edge      = false;
        }
    }

    /* ── Dispatch to active state handler ────────────────── */
    switch (ctx.master) {
        case STATE_DEFAULT:  handle_default_state();  break;
        case STATE_AI:       handle_ai_state();        break;
        case STATE_SETTINGS: handle_settings_state();  break;
        default: break;
    }
}
/* ─── APP MAIN ──────────────────────────────────────────── */
void app_main(void)
{
    /* GPIO config – unchanged */
    gpio_config_t io_conf = {
        .pin_bit_mask   = (1ULL << TOUCH1) | (1ULL << TOUCH2),
        .mode           = GPIO_MODE_INPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_POSEDGE,
    };
    gpio_config(&io_conf);
    printf("gpio_configured\n");

    gpio_install_isr_service(0);
    gpio_isr_handler_add(TOUCH1, touch_isr_handler, (void *)(uintptr_t)TOUCH1);
    gpio_isr_handler_add(TOUCH2, touch_isr_handler, (void *)(uintptr_t)TOUCH2);
    printf("isr ready\n");

    /* Timer creation */
    const esp_timer_create_args_t timers[] = {
        { .callback = tap_timer_cb,       .name = "tap_timer" },
        { .callback = hold_timer_cb,      .name = "hold_timer" },
        { .callback = t2_hold_timer_cb,   .name = "t2_hold" },
        { .callback = dual_hold_timer_cb, .name = "dual_hold" },
    };
    esp_timer_handle_t *handles[] = { &tap_timer, &hold_timer, &t2_hold_timer, &dual_hold_timer };
    for (int i = 0; i < 4; i++)
        esp_timer_create(&timers[i], handles[i]);

    printf("pebo HSM ready\n");

    while (1) {
        process_master_state();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}