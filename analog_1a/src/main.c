#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "driver/i2s_std.h"
#include "helpers.h" // find_idle_value, convert_adc_sample, scale_adc_to_i2s
#include "constants.h"

// handlers
adc_continuous_handle_t adc_handle = NULL;
QueueHandle_t adc_queue = NULL;

// initialize the adc continuous driver on ADC1 channel 0 (GPIO36)
void adc_init(adc_continuous_handle_t* handle_ptr) {
    adc_continuous_handle_cfg_t adc_init_config = {
        .max_store_buf_size = FRAME_SIZE * 4,
        .conv_frame_size = FRAME_SIZE // in bytes
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_init_config, handle_ptr));

    adc_continuous_config_t adc_read_config = {
        .sample_freq_hz = 44100, // 44.1 khz
        .pattern_num = 1,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1, // ADC1 only
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1
    };

    adc_digi_pattern_config_t adc_pattern[1] = {
        {
            .atten = ADC_ATTEN_DB_6,
            .channel = ADC_CHANNEL_0, // ADC1 channel 0 (GPIO36)
            .unit = ADC_UNIT_1,
            .bit_width = ADC_BITWIDTH_12
        }
    };

    adc_read_config.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(*handle_ptr, &adc_read_config));
    printf("ADC continuous driver initialized\n");
}

// fills data from adc stream once.
// will fail if init & start not called first
void adc_read_once(adc_continuous_handle_t* handle_ptr, uint8_t* data, bool dbg) {
    esp_err_t ret;
    uint32_t delay, retnum;
    if (dbg) delay = 500; else delay = 30; // ms to wait for data, 30ms is enough for 44.1kHz
    ret = adc_continuous_read(*handle_ptr, data, FRAME_SIZE, &retnum, delay); // 500 ms for dbg, 30ms actual
    
    if (ret == ESP_OK) {
        if (dbg) {
            printf("Read %lu bytes\n", (unsigned long)retnum);
            //for (uint32_t i = 0; i < *retnum; i += 2) { // print one value vs continuous stream
                uint16_t sample = convert_adc_sample(data[0], data[1]); // lower 12 bits are adc
                printf("%d\n", sample);
            //}
            vTaskDelay(pdMS_TO_TICKS(delay));
        }
    } else if (ret == ESP_ERR_TIMEOUT) {
        printf("ADC read timeout\n");
    } else {
        printf("ADC read error: %d\n", ret);
    }
}

// task to read adc data continuously
void adc_read_task(void *param) {
    while (1) {
        uint8_t* data = malloc(FRAME_SIZE); // buffer in pure bytes
        adc_read_once(&adc_handle, data, false);
        printf("Data read from ADC: %d\n", *data); // print first byte for debug
        
        if(xQueueSend(adc_queue, &data, pdMS_TO_TICKS(30)) != pdTRUE) {
            printf("Failed to send data to queue\n");
            free(data); // free if send failed. needs to freed on receiver end if send succeeds
        } // send data to queue
        else { // dbg only
            printf("Data sent to queue\n");
            uint8_t* temp = NULL;
            if (xQueueReceive(adc_queue, &temp, pdMS_TO_TICKS(1000))) {
                printf("Received from self: %d\n", *temp);
                free(temp);
            }
        }
    }
}

void app_main(void) {
    uint16_t idle_adc_val;
    uint8_t data[FRAME_SIZE];

    // queue for adc readings
    adc_queue = xQueueCreate(6, sizeof(uint8_t*)); // 6 items, each item is a pointer to data
    if (adc_queue == NULL) {
        printf("Failed to create xqueue\n");
        return;
    }

    adc_init(&adc_handle);

    // start ADC continuous sampling
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

    // find idle value 
    adc_read_once(&adc_handle, data, false); 
    idle_adc_val = find_idle_value(data); // find idle value for i2s scaling

    // start threads
    xTaskCreate(adc_read_task, "adc_read_task", 2048, NULL, 5, NULL);

    // stop adc 
    //ESP_ERROR_CHECK(adc_continuous_stop(adc_handle));
    //ESP_ERROR_CHECK(adc_continuous_deinit(adc_handle));

}
