#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "espnow.h"
#include "i2s_audio.h"
#include "driver/gpio.h"

#define ESPNOW_MAXDELAY 512
#define QUEUE_SEND_DELAY 100
#define QUEUE_RECV_DELAY 100
#define MY_CODE 1234
#define GPIO_INPUT_PIN GPIO_NUM_0
#define ESPNOW_DATA_SIZE 128

static uint8_t s_example_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t s_example_espnow_seq[EXAMPLE_ESPNOW_DATA_MAX] = {0, 0};
static const char *TAG = "espnow";

static void example_espnow_deinit(esp_now_data_packet *data_packet);
static uint16_t *recive_16bit;                   // 接收后转换的16位数据，长度128
static uint16_t *output_16bit;                   // 发送给扬声器的16位数据，长度256，因为传入数据是双声道所以*2
static esp_now_data_packet *send_broadcast_data; // 用于发送广播配对
static bool send_over = true;

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
static void example_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    send_over = true;
}

static void example_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    example_espnow_event_recv_cb_t recv_cb;
    uint8_t *mac_addr = recv_info->src_addr;
    uint8_t *des_addr = recv_info->des_addr;

    if (mac_addr == NULL || data == NULL || len <= 0)
    {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    if (IS_BROADCAST_ADDR(des_addr))
    {
        /* If added a peer with encryption before, the receive packets may be
         * encrypted as peer-to-peer message or unencrypted over the broadcast channel.
         * Users can check the destination address to distinguish it.
         */
        ESP_LOGI(TAG, "Receive broadcast ESPNOW data");
    }
    esp_now_data_packet *recv_data = (esp_now_data_packet *)data;

    if (10000 == recv_data->code)
    {
        ESP_LOGI(TAG, "Receive peer apply from: " MACSTR "", MAC2STR(mac_addr));
        memcpy(recv_cb.mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
        /*如果没有配对过，则加入配对*/
        if (esp_now_is_peer_exist(recv_cb.mac_addr) == false)
        {
            esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
            if (peer == NULL)
            {
                ESP_LOGE(TAG, "Malloc peer information fail");
                example_espnow_deinit(send_broadcast_data);
                vTaskDelete(NULL);
            }
            memset(peer, 0, sizeof(esp_now_peer_info_t));
            peer->channel = CONFIG_ESPNOW_CHANNEL;
            peer->ifidx = ESPNOW_WIFI_IF;
            peer->encrypt = true;
            memcpy(peer->peer_addr, recv_cb.mac_addr, ESP_NOW_ETH_ALEN);
            ESP_ERROR_CHECK(esp_now_add_peer(peer));
            free(peer);
        }
        else
        {
            ESP_LOGI(TAG, "Peer already exist");
        }
    }
    else if (MY_CODE == recv_data->code)
    {

        int payload_len = len - sizeof(esp_now_data_packet);
        // 将recv_data->data转为16bit数据
        for (int i = 0; i < ESPNOW_DATA_SIZE; i++)
        {
            recive_16bit[i] = (data[i] - 128) << 5;
            output_16bit[i * 2] = recive_16bit[i];
            output_16bit[i * 2 + 1] = recive_16bit[i];
        }

        if (!i2s_write_max98357(output_16bit, ESPNOW_DATA_SIZE))
        {
            ESP_LOGE(TAG, "i2s write failed");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Receive wrong data");
    }
}

static esp_err_t espnow_init(void)
{
    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(example_espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(example_espnow_recv_cb));
#if CONFIG_ESPNOW_ENABLE_POWER_SAVE
    ESP_ERROR_CHECK(esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW));
    ESP_ERROR_CHECK( esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL) ));
#endif
    /* Set primary master key. */
    // ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK));
    return ESP_OK;
}

static esp_err_t init_broadcast_send_data()
{
    send_broadcast_data = malloc(sizeof(esp_now_data_packet));
    if (send_broadcast_data == NULL)
    {
        ESP_LOGE(TAG, "Malloc broadcast send data fail");
        esp_now_deinit();
        return ESP_FAIL;
    }
    int len = sizeof(esp_now_data_packet);
    memset(send_broadcast_data, 0, len);
    send_broadcast_data->code = 10000;
    send_broadcast_data->data[0] = 0;
    return ESP_OK;
}

static bool check_peer_more_than_boradcast()
{
    uint8_t peerNum = 0;
    ESP_ERROR_CHECK(esp_now_get_peer_num(&peerNum));
    ESP_LOGI(TAG, "peerNum: %d", peerNum);
    if (peerNum > 1)
    {
        return true;
    }
    return false;
}

/*通过广播自动配对，先发送广播，收到回应后及开始配对*/
esp_err_t auto_peer(void)
{
    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL)
    {
        ESP_LOGE(TAG, "Malloc peer information fail");
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);

    uint8_t peerNum = 0;
    ESP_LOGI(TAG, "Start sending broadcast data for peer");
    int trycount = 1;
    while (trycount <= 10)
    {
        ESP_LOGI(TAG, "sending broadcast data try count: %d", trycount);
        /* Start sending broadcast ESPNOW data. */
        esp_err_t temp1 = esp_now_send(&s_example_broadcast_mac, send_broadcast_data, 16);

        ESP_LOGI(TAG, "temp1: %d", temp1);
        if (temp1 != ESP_OK)
        {
            ESP_LOGE(TAG, "Send error");
            example_espnow_deinit(send_broadcast_data);
            vTaskDelete(NULL);
        }
        trycount++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return ESP_OK;
}

static void example_espnow_deinit(esp_now_data_packet *data_packet)
{
    free(data_packet->data);
    free(data_packet);
    esp_now_deinit();
}

void read_data_task(void *args)
{

    esp_now_peer_info_t peer_uni;
    esp_now_fetch_peer(true, &peer_uni);

    int send_len = ESPNOW_DATA_SIZE + sizeof(esp_now_data_packet);
    uint8_t *r_buf = (uint8_t *)calloc(1, ESPNOW_DATA_SIZE);
    assert(r_buf); // Check if s_buf allocation success

    esp_now_data_packet *send_data;
    send_data = malloc(send_len);
    if (send_data == NULL)
    {
        ESP_LOGE(TAG, "Malloc send data fail");
        esp_now_deinit();
        return;
    }
    send_data->code = MY_CODE;

    while (1)
    {
        size_t r_bytes = 0;
        // Check if the button is pressed
        if (gpio_get_level(GPIO_INPUT_PIN) != 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        i2s_read_npm441(r_buf, &r_bytes, ESPNOW_DATA_SIZE);

        memcpy(send_data->data, r_buf, ESPNOW_DATA_SIZE);

        while (!send_over)
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        send_over = false;
        esp_err_t err_t = esp_now_send(peer_uni.peer_addr, send_data, send_len);
        if (err_t != ESP_OK)
        {
            ESP_LOGE(TAG, "Send error to " MACSTR ", err_t: %d", MAC2STR(peer_uni.peer_addr), err_t);
        }
    }
    free(send_data);
}

void espnow_create_send_i2s_task(void)
{
    xTaskCreate(read_data_task, "read_data_task", 1024, NULL, 4, NULL);
}

void test_send_sine_wave(void)
{
    int buffer_size = ESPNOW_DATA_SIZE / 2;
    uint16_t buffer[buffer_size];
    generate_sine_wave(buffer, buffer_size, 16000);

    esp_now_peer_info_t peer_uni;
    esp_now_fetch_peer(true, &peer_uni);

    int send_len = ESPNOW_DATA_SIZE + sizeof(esp_now_data_packet);
    uint8_t *r_buf = (uint8_t *)calloc(1, ESPNOW_DATA_SIZE);
    assert(r_buf); // Check if s_buf allocation success

    for (uint8_t i = 0; i < buffer_size; i++)
    {
        r_buf[i * 2] = (uint8_t)(buffer[i] >> 8);
        r_buf[i * 2 + 1] = (uint8_t)(buffer[i] & 0xff);
    }

    esp_now_data_packet *send_data;
    send_data = malloc(send_len);
    if (send_data == NULL)
    {
        ESP_LOGE(TAG, "Malloc send data fail");
        esp_now_deinit();
        return;
    }
    send_data->code = MY_CODE;
    memcpy(send_data->data, r_buf, ESPNOW_DATA_SIZE);
    int count = 782;
    while (1)
    {
        while (count > 0)
        {
            while (!send_over)
            {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            count--;
            send_over = false;
            esp_err_t err_t = esp_now_send(peer_uni.peer_addr, send_data, send_len);
            if (err_t != ESP_OK)
            {
                ESP_LOGE(TAG, "Send error to " MACSTR ", err_t: %d", MAC2STR(peer_uni.peer_addr), err_t);
            }
        }
        count = 782;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    free(send_data);
}

void espnow_global_init(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init();
    ESP_ERROR_CHECK(espnow_init());
    ESP_ERROR_CHECK(init_broadcast_send_data());
    ESP_ERROR_CHECK(auto_peer());
    recive_16bit = (uint16_t *)malloc(sizeof(uint16_t) * ESPNOW_DATA_SIZE);
    output_16bit = (uint16_t *)malloc(2 * sizeof(uint16_t) * ESPNOW_DATA_SIZE);

    // Check if memory allocation was successful
    if (recive_16bit == NULL || output_16bit == NULL)
    {
        ESP_LOGE(TAG, "Memory allocation failed\n");
        exit(1);
    }
}