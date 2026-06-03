#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define touch1 GPIO_NUM_2
volatile bool touched = false;

void IRAM_ATTR touch_isr_handler(void* arg){
    touched = true;
}

void app_main(void)
{
    printf("start program \n");

    gpio_config_t io_conf={
        .pin_bit_mask = (1ULL<< touch1),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en= GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&io_conf);
    printf("gpio_configured\n ");

    gpio_install_isr_service(0);
    printf("isr service installed\n");
    gpio_isr_handler_add(touch1,touch_isr_handler,NULL);
    printf("isr handler added\n");

    while(1){
        if (touched)
        {
            touched = false;
            printf("touch detected");
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf("cpu handling other task.....\n");
    }
}
