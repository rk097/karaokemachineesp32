#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_err.h"
#include "esp_a2dp_api.h"
#include "freertos/ringbuf.h"
#include "Bluetooth.h"

#define TAG "A2DP"

// not ideal, but better than extern
unsigned long RINGBUFFER_CAPACITY; 
RingbufHandle_t* bt_ringbuf_ptr;
bool* bt_playing_ptr;

// on connection request
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
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
static void bt_app_a2d_data_cb(const uint8_t* data, uint32_t len) {
    // write to ringbuffer. separate thread will write to i2s
    if (xRingbufferSend(*bt_ringbuf_ptr, data, len, 0) != pdTRUE) { // write failed. callback must be nonblocking
        ESP_LOGI(TAG, "Ringbuffer is full");
        return;
    }
}

// Bluetooth event callback
static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
    ESP_LOGI(TAG, "A2DP event: %d", event);
    esp_a2d_cb_param_t* a2d = param;
    assert(a2d != NULL);

    switch(event) {
        case ESP_A2D_CONNECTION_STATE_EVT: // handle a2dp connections
            if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                if ((*bt_ringbuf_ptr = xRingbufferCreate(RINGBUFFER_CAPACITY, RINGBUF_TYPE_BYTEBUF)) == NULL) {
                    ESP_LOGE(TAG, "%s ringbuffer create failed", __func__);
                    return;
                } // ringbuf needs to exist before a2dp connnections
                *bt_playing_ptr = false;
                ESP_LOGI(TAG, "Ringbuffer created");
            } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                *bt_playing_ptr = false;
            }
            break;
        case ESP_A2D_AUDIO_CFG_EVT: // when audio codec configure
            // configure frequency. might not do this
            ESP_LOGI(TAG, "config A2DP event: %d", event);
            break;
        case ESP_A2D_AUDIO_STATE_EVT: // pause, play
            if (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) *bt_playing_ptr = true;
            else if (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_SUSPEND) *bt_playing_ptr = false;
            break;
        default:
            ESP_LOGI(TAG, "Unhandled A2DP event: %d", event);
            break;
    }
}

void bt_init(RingbufHandle_t* ringbuf_handle_ptr, unsigned long ringbuf_size, bool* bt_playing) {
    ESP_LOGI("TEST", "Start");
    RINGBUFFER_CAPACITY = ringbuf_size;
    bt_ringbuf_ptr = ringbuf_handle_ptr;
    bt_playing_ptr = bt_playing;

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