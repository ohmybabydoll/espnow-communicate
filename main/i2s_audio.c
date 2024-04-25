#include <stdint.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "i2s_audio.h"
#include "math.h"

#define EXAMPLE_STD_BCLK_IO1 GPIO_NUM_26 // I2S bit clock io number
#define EXAMPLE_STD_WS_IO1 GPIO_NUM_22   // I2S word select io number
#define EXAMPLE_STD_DOUT_IO1 GPIO_NUM_25 // I2S data out io number
#define EXAMPLE_STD_DIN_IO1 GPIO_NUM_NC  // I2S data in io number

#define EXAMPLE_STD_BCLK_IO2 GPIO_NUM_4  // I2S bit clock io number
#define EXAMPLE_STD_WS_IO2 GPIO_NUM_15   // I2S word select io number
#define EXAMPLE_STD_DOUT_IO2 GPIO_NUM_NC // I2S data out io number (not used)
#define EXAMPLE_STD_DIN_IO2 GPIO_NUM_13  // I2S data in io number

static const char *I2S_TAG = "i2s";

static i2s_chan_handle_t tx_chan; // I2S tx channel handler
static i2s_chan_handle_t rx_chan; // I2S rx channel handler

bool i2s_read_npm441(uint8_t *r_buf, size_t *r_bytes, int read_len)
{
    return i2s_channel_read(rx_chan, r_buf, read_len, r_bytes, portMAX_DELAY) == ESP_OK;
}

bool i2s_write_max98357(uint16_t *w_buf, int w_lenth)
{
    size_t w_bytes = 0;
    bool result = i2s_channel_write(tx_chan, w_buf, w_lenth, &w_bytes, portMAX_DELAY) == ESP_OK;
    return result;
}

static void init_npm441()
{
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    // rx_chan_cfg.dma_desc_num = 2;
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),                                              // 44.1kHz sample rate
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO), // 16-bit data width, mono mode
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, // some codecs may require mclk signal, this example doesn't need it
            .bclk = EXAMPLE_STD_BCLK_IO2,
            .ws = EXAMPLE_STD_WS_IO2,
            .dout = EXAMPLE_STD_DOUT_IO2,
            .din = EXAMPLE_STD_DIN_IO2,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    /* Default is only receiving left slot in mono mode,
     * update to right here to show how to change the default configuration */
    // rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg));
}

static void init_max98357()
{
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    tx_chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = EXAMPLE_STD_BCLK_IO1,
            .ws = EXAMPLE_STD_WS_IO1,
            .dout = EXAMPLE_STD_DOUT_IO1,
            .din = EXAMPLE_STD_DIN_IO1,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
}

void generate_sine_wave(uint16_t *w_buf, int w_lenth, int s_rate)
{
    int pi = 3.1415926;
    int freq = 440.0;
    for (int i = 0; i < w_lenth; i++)
    {
        double t = (double)i / s_rate;
        w_buf[i] = (uint16_t)(sin(2 * pi * freq * t) * INT16_MAX);
    }
}

void test_i2s_audio(void)
{
    int buffer_size = 160;
    uint16_t buffer[buffer_size];
    generate_sine_wave(buffer, buffer_size, 16000);
    while (1)
    {
        int count = 0;
        while (count < 100)
        {
            i2s_write_max98357(buffer, buffer_size);
            count++;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void init_i2s_audio()
{
    init_npm441();
    init_max98357();
    /* Enable the RX channel */
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}