#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/Task.h"
#include "driver/gpio.h"

#define button GPIO_NUM_5
#define led GPIO_NUM_2

void app_main(void)
{
    gpio_reset_pin(led);
    gpio_reset_pin(button);

    gpio_set_direction(led,GPIO_MODE_OUTPUT);
    gpio_set_direction(button,GPIO_MODE_INPUT);

    while(1){

        if(gpio_get_level(button) == 0)
        {
            gpio_set_level(led,1);
        }
        gpio_set_level(led,0);

    }

}
