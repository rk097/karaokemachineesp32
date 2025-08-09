#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_err.h"
#include "esp_a2dp_api.h"
#include "freertos/ringbuf.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include "string.h"

#include "constants.h"
#include "I2S.h"

#define TAG "A2DP"
#define RINGBUFFER_CAPACITY sizeof(int32_t) * FRAME_SIZE * DMA_BUFFER_COUNT

// globals
static int32_t global_buffer[DMA_BUFFER_COUNT][FRAME_SIZE]; // buffer roll for i2s mic input.
i2s_chan_handle_t i2s_in_handle = NULL; // i2s mic input stream
i2s_chan_handle_t i2s_out_handle = NULL; // i2s output stream
RingbufHandle_t bt_ringbuf = NULL; // bluetooth input bytes
QueueHandle_t i2s_queue_free = NULL;
QueueHandle_t i2s_queue_busy = NULL;
static bool bt_playing = false;

void temp_i2s_in_init();
void temp_i2s_out_init();
void i2s_read_task();
void i2s_write_task();

// on connection request
void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
    switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
                ESP_LOG_BUFFER_HEX(TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
            } else {
                ESP_LOGE(TAG, "Authentication failed, status:%d", param->auth_cmpl.stat);
            }
            break;

        case ESP_BT_GAP_PIN_REQ_EVT:
            ESP_LOGI(TAG, "PIN requested. Using default 1234");
            esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            break;

        case ESP_BT_GAP_CFM_REQ_EVT:
            ESP_LOGI(TAG, "SSP confirm requested. Accepting...");
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        default:
            ESP_LOGI(TAG, "Unhandled GAP event: %d", event);
            break;
    }
}

// Dummy audio data handler
void bt_app_a2d_data_cb(const uint8_t* data, uint32_t len) {
    // write to ringbuffer. separate thread will write to i2s
    if (xRingbufferSend(bt_ringbuf, data, len, 0) != pdTRUE) { // write failed. callback must be nonblocking
        ESP_LOGI(TAG, "Ringbuffer is full");
        return;
    }
}

// Bluetooth event callback
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
    ESP_LOGI(TAG, "A2DP event: %d", event);
    esp_a2d_cb_param_t* a2d = param;
    assert(a2d != NULL);

    switch(event) {
        case ESP_A2D_CONNECTION_STATE_EVT: // handle a2dp connections
            if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                if ((bt_ringbuf = xRingbufferCreate(RINGBUFFER_CAPACITY, RINGBUF_TYPE_BYTEBUF)) == NULL) {
                    ESP_LOGE(TAG, "%s ringbuffer create failed", __func__);
                    return;
                } // ringbuf needs to exist before a2dp connnections
                bt_playing = false;
                ESP_LOGI(TAG, "Ringbuffer created");
            } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                bt_playing = false;
            }
            break;
        case ESP_A2D_AUDIO_CFG_EVT: // when audio codec configure
            // configure frequency. might not do this
            ESP_LOGI(TAG, "config A2DP event: %d", event);
            break;
        case ESP_A2D_AUDIO_STATE_EVT: // pause, play
            if (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) bt_playing = true;
            else if (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_SUSPEND) bt_playing = false;
            break;
        default:
            ESP_LOGI(TAG, "Unhandled A2DP event: %d", event);
            break;
    }
}

// initializes nvs and bluetooth stack.
void bt_init();

void app_main(void)
{       
    // queue for incoming i2s data
    i2s_queue_free = xQueueCreate(DMA_BUFFER_COUNT, sizeof(int32_t*)); // store pointers to i2s data buffers
    if (i2s_queue_free == NULL) {
        ESP_LOGE(TAG, "%s free queue create failed", __func__);
        return;
    }

    // set all initial buffers to free
    for (uint8_t i = 0; i < DMA_BUFFER_COUNT; i++) {
        int32_t* p = global_buffer[i];
        xQueueSend(i2s_queue_free, &p, portMAX_DELAY);
    }

    // queue for outstream i2s data
    i2s_queue_busy = xQueueCreate(DMA_BUFFER_COUNT, sizeof(int32_t*)); // store pointers to i2s data buffers
    if (i2s_queue_busy == NULL) {
        ESP_LOGE(TAG, "%s busy queue create failed", __func__);
        vQueueDelete(i2s_queue_free);
        return;
    }

    // init i2s
    temp_i2s_in_init();
    temp_i2s_out_init();

    ESP_ERROR_CHECK(i2s_channel_enable(i2s_in_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_out_handle));
    ESP_LOGI(TAG, "I2S enabled");
    xTaskCreate(i2s_write_task, "i2s_write_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "I2S Write Task has begun");
    xTaskCreate(i2s_read_task, "i2s_read_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "I2S Read Task has begun");

    bt_init();
}

// read in i2s 
void i2s_read_task(void* param) {
    int32_t* raw_input_buffer;
    while(1) {
        if (xQueueReceive(i2s_queue_free, &raw_input_buffer, portMAX_DELAY) == pdTRUE) { // Queue send and receive work with pointers of pointers
            if (i2s_read_once(&i2s_in_handle, raw_input_buffer, FRAME_SIZE) == ESP_OK) {
                if (xQueueSend(i2s_queue_busy, &raw_input_buffer, portMAX_DELAY) != pdTRUE) {
                    printf("Could not send data to busy queue\n");
                } 
            }
        }
    }
}

// i2s output to speaker
void i2s_write_task(void *param) {
    uint8_t* byte_data = NULL;
    int32_t* i2s_mic_data = NULL;
    while (1) {
        int16_t output_buffer[FRAME_SIZE*2] = {0};
        if (bt_playing) {
            // first handle a2dp stuff if bluetooth is on
            size_t item_size = 0;
            byte_data = xRingbufferReceiveUpTo(bt_ringbuf, &item_size, pdMS_TO_TICKS(20), 2*sizeof(int16_t) * FRAME_SIZE);
            if (item_size != 0) {
                for (int i = 0; i < item_size; i+=2) {
                    output_buffer[i/2] = ((int16_t)(((uint16_t)byte_data[i+1] << 8) | byte_data[i]));
                }
                
                vRingbufferReturnItem(bt_ringbuf, byte_data);    
            }
        }
        
        // then pipe in mic if successful
        if (xQueueReceive(i2s_queue_busy, &i2s_mic_data, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (i2s_mic_data != NULL) {
                for (int i = 0; i < FRAME_SIZE*2; i+=2) {
                    int16_t mic_reading_16 = (int16_t)(i2s_mic_data[i / 2] >> 16);
                    output_buffer[i] += (mic_reading_16);
                    output_buffer[i+1] += (mic_reading_16);
                }
            }
            if (xQueueSend(i2s_queue_free, &i2s_mic_data, pdMS_TO_TICKS(20)) != pdTRUE) {
                printf("Could not return buffer to free queue\n");
            } 
        } else {
            printf("Failed to receive i2s data from queue\n");
        }    

        // write to i2s.
        i2s_channel_write(i2s_out_handle, output_buffer, FRAME_SIZE*sizeof(int16_t)*2, NULL, portMAX_DELAY);  
    }
}

void temp_i2s_in_init() {
    int dma_buffer_count = DMA_BUFFER_COUNT;
    int frame_size = FRAME_SIZE;
    int sample_rate = SAMPLE_RATE;
    i2s_chan_handle_t* input_chan_ptr = &i2s_in_handle;
    // INPUT
    // initialize i2s channel and i2s settings
    i2s_chan_config_t i2s_chan_config_1 = {  // shared between input and output
        .id = I2S_NUM_0, 
        .role = I2S_ROLE_MASTER, 
        .dma_desc_num = dma_buffer_count, 
        .dma_frame_num = frame_size, 
        .auto_clear_after_cb = false, 
        .auto_clear_before_cb = false,
        .allow_pd = false, 
        .intr_priority = 0, 
    };
    ESP_ERROR_CHECK(i2s_new_channel(&i2s_chan_config_1, NULL, input_chan_ptr));
    
    i2s_std_config_t i2s_in_config = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_33,
            .ws   = GPIO_NUM_32,
            .dout = I2S_GPIO_UNUSED,
            .din  = GPIO_NUM_34,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*input_chan_ptr, &i2s_in_config));
    printf("I2S input driver initialized\n");
}

void temp_i2s_out_init() {
    int dma_buffer_count = DMA_BUFFER_COUNT;
    int frame_size = FRAME_SIZE;
    int sample_rate = SAMPLE_RATE;
    i2s_chan_handle_t* output_chan_ptr = &i2s_out_handle;
    // OUTPUT
    i2s_chan_config_t i2s_chan_config_2 = {  // shared between input and output
        .id = I2S_NUM_1, 
        .role = I2S_ROLE_MASTER, 
        .dma_desc_num = dma_buffer_count, 
        .dma_frame_num = frame_size, 
        .auto_clear_after_cb = false, 
        .auto_clear_before_cb = false,
        .allow_pd = false, 
        .intr_priority = 0, 
    };
    ESP_ERROR_CHECK(i2s_new_channel(&i2s_chan_config_2, output_chan_ptr, NULL));

    i2s_std_config_t i2s_out_config = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_26,
            .ws   = GPIO_NUM_25,
            .dout = GPIO_NUM_22,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*output_chan_ptr, &i2s_out_config));
    printf("I2S output driver initialized\n");
}

void bt_init() {
    ESP_LOGI("TEST", "Start");

    // Release BLE memory first
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    ESP_LOGI("TEST", "BLE mem released");

    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI("TEST", "NVS inited");

    // Init BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_LOGI("TEST", "BT controller inited");

    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_LOGI("TEST", "BT controller enabled");

    // Init Bluedroid
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_LOGI("TEST", "Bluedroid enabled");

    // Set device name and visibility
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name("ESP32 Karaoke"));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));

    ESP_LOGI("TEST", "BT ready");

    // Set SSP IO capabilities
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t)));

    // Set fallback PIN (e.g., 1234)
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
    ESP_ERROR_CHECK(esp_bt_gap_set_pin(pin_type, 4, pin_code));
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(bt_app_gap_cb));

    // Register A2DP callbacks and initialize sink
    ESP_ERROR_CHECK(esp_a2d_register_callback(&bt_app_a2d_cb));
    ESP_ERROR_CHECK(esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb));
    ESP_ERROR_CHECK(esp_a2d_sink_init());

    ESP_LOGI(TAG, "A2DP sink initialized and discoverable");
}