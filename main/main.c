#include <stdio.h>
#include <stdlib.h>
#include <espnow.h>
#include <i2s_audio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "common.h"
#include "esp_random.h"
#include "esp_mac.h"


#define GPIO_INPUT_PIN GPIO_NUM_0

static const char *READ_I2S_TAG = "READ I2S";

void app_main(void)
{
    init_i2s_audio();
    espnow_global_init();
    test_send_sine_wave();
    //espnow_create_send_i2s_task();
    //test_i2s_audio();
}
