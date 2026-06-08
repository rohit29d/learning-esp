#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

struct StateMachine {
    int state; // states : 1,2,3
};

#define touch1 GPIO_NUM_2
#define touch2 GPIO_NUM_3

volatile bool trig1 = false;
volatile bool trig2 = false; 
static volatile bool tap_window_expired = false;
static volatile bool hold_timer_expired = false;
static int tap_count = 0;
static esp_timer_handle_t tap_timer;
static esp_timer_handle_t hold_timer;

static void tap_timer_callback(void* arg)
{
    tap_window_expired = true;
}

static void hold_timer_callback(void* arg)
{
    hold_timer_expired = true;
}

void IRAM_ATTR touch_isr_handler(void* arg){
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;

    if(gpio_num == touch1){
        trig1 = true;
    }
    else if(gpio_num == touch2){
        trig2= true;
    }
}

void app_main(void)
{
    gpio_config_t io_conf={
        .pin_bit_mask = (1ULL << touch1) | (1ULL << touch2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en= GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&io_conf);
    printf("gpio_configured\n ");

    gpio_install_isr_service(0);
    printf("isr service installed\n");
    gpio_isr_handler_add(touch1, touch_isr_handler, (void *)(uintptr_t)touch1);
    gpio_isr_handler_add(touch2, touch_isr_handler, (void *)(uintptr_t)touch2);
    printf("isr handler added\n");

    const esp_timer_create_args_t tap_timer_args = {
        .callback = tap_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "tap_timer",
    };
    esp_timer_create(&tap_timer_args, &tap_timer);

    const esp_timer_create_args_t hold_timer_args = {
        .callback = hold_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "hold_timer",
    };
    esp_timer_create(&hold_timer_args, &hold_timer);

    int64_t next_default_log_us = 0;

    while(1){
        int64_t now_us = esp_timer_get_time();
        if(now_us >= next_default_log_us){
            printf("--state1 : default--\n");
            next_default_log_us = now_us + 1000000;
        }

        if(trig1 || trig2){
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        if((trig1 || trig2) && gpio_get_level(touch1) == 1 && gpio_get_level(touch2) == 1){
            printf("--state3 - settings--\n");
            trig1 = false;
            trig2 = false;
            tap_count = 0;
            tap_window_expired = false;
            hold_timer_expired = false;
            esp_timer_stop(tap_timer);
            esp_timer_stop(hold_timer);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        else if(trig1){
            trig1 = false;

            if(tap_count == 0){
                tap_count++;
                tap_window_expired = false;
                hold_timer_expired = false;
                esp_timer_start_once(tap_timer, 300000);
                esp_timer_start_once(hold_timer, 1000000);
            }
            else if(!tap_window_expired && tap_count == 1){
                tap_count++;
                esp_timer_stop(tap_timer);
                tap_window_expired = false;
                esp_timer_start_once(tap_timer, 300000);
            }
            else if(!tap_window_expired && tap_count == 2){
                tap_count++;
                printf("TRIPLE_TAP\n");
                printf("--state1: substate3\n");
                tap_count = 0;
                tap_window_expired = false;
                hold_timer_expired = false;
                esp_timer_stop(tap_timer);
                esp_timer_stop(hold_timer);
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
        }
        else if(trig2){
            trig2 = false;
        }

        if(tap_window_expired && tap_count > 0){
            if(tap_count == 1){
                if(gpio_get_level(touch1) == 0){
                    printf("SINGLE_TAP\n");
                    printf("--state1: substate1\n");
                    tap_count = 0;
                    tap_window_expired = false;
                    hold_timer_expired = false;
                    esp_timer_stop(hold_timer);
                }
            }
            else if(tap_count == 2){
                printf("DOUBLE_TAP\n");
                printf("--state1: substate2\n");
                tap_count = 0;
                tap_window_expired = false;
                hold_timer_expired = false;
                esp_timer_stop(hold_timer);
            }
        }

        if(hold_timer_expired && tap_count == 1 && gpio_get_level(touch1) == 1){
            printf("HOLD\n");
            tap_count = 0;
            tap_window_expired = false;
            hold_timer_expired = false;
            esp_timer_stop(tap_timer);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
