#include <stdio.h>
#include <stdlib.h>
#include <espnow.h>
#include <i2s_audio.h>

void app_main(void)
{
    espnow_global_init();
    init_i2s_audio();
}
