#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define touch1 GPIO_NUM_2

void app_main(void)
{
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL<< touch1),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE, 
    };

    gpio_config(&io_config);

    while(1)
    {
        int level = gpio_get_level(touch1);

        if(level == 1){
            printf("touch detected \n");

        }else{
            printf("touch not detected \n");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

    }
}
