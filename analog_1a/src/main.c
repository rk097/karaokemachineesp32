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
QueueHandle_t i2s_queue = NULL;

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
esp_err_t adc_read_once(adc_continuous_handle_t* handle_ptr, uint8_t* data, bool dbg) {
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

    return ret;
}

// task to read adc data continuously
void adc_read_task(void *param) {
    while (1) {
        uint8_t* data = malloc(FRAME_SIZE); // buffer in pure bytes
        if (adc_read_once(&adc_handle, data, false) == ESP_OK) { // safeguard against sending garbage into queue
            if(xQueueSend(adc_queue, &data, pdMS_TO_TICKS(100)) != pdTRUE) { // send data to queue
                printf("Failed to send data to queue\n");
                free(data); // free if send failed. needs to freed on receiver end if send succeeds
            } 
        }
    }
}

// adc to i2s processing task
void adc_to_i2s_task(void *param) {
    uint8_t* data = malloc(FRAME_SIZE); 
    uint16_t idle_adc_val;
    // find idle value 
    while(adc_read_once(&adc_handle, data, false) != ESP_OK) {}; // make sure we have valid data for idle calibration
    idle_adc_val = find_idle_value(data); // find idle value for i2s scaling
    free(data); // free after finding idle value
    data = NULL; // reset pointer

    while (1) {
        if (xQueueReceive(adc_queue, &data, portMAX_DELAY) == pdTRUE) {
            if (data != NULL) {
                // scale to i2s format
                int16_t* i2s_data = malloc(FRAME_SIZE/2 * sizeof(int16_t)); // allocate memory for i2s data
                for (uint16_t i = 0; i < FRAME_SIZE; i += 2) {
                    uint16_t sample = convert_adc_sample(data[i], data[i + 1]); // lower 12 bits are adc
                    i2s_data[i/2] = scale_adc_to_i2s(sample, idle_adc_val); // 2048 adc half-samples filled into 1024 i2s samples
                    // For example: i2s_write(I2S_NUM_0, &scaled_value, sizeof(scaled_value), &bytes_written, portMAX_DELAY);
                }
                free(data); // free after processing
                if(xQueueSend(i2s_queue, &i2s_data, portMAX_DELAY) != pdTRUE) {
                    printf("Failed to send i2s data to queue\n");
                    free(i2s_data); // free if send failed
                } 
            }
        } else {
            printf("Failed to receive data from queue\n");
        }
    }
}

void app_main(void) {
    // queue for adc readings
    adc_queue = xQueueCreate(6, sizeof(uint8_t*)); // store pointers to data buffers to prevent unnecessary copies
    if (adc_queue == NULL) {
        printf("Failed to create xqueue\n");
        return;
    }

    // queue for i2s data
    i2s_queue = xQueueCreate(6, sizeof(int16_t*)); // store pointers to i2s data buffers
    if (i2s_queue == NULL) {
        printf("Failed to create i2s queue\n");
        vQueueDelete(adc_queue); // cleanup adc queue
        return;
    }

    adc_init(&adc_handle);

    // start ADC continuous sampling
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
    vTaskDelay(pdMS_TO_TICKS(1000)); // give some time for adc to start

    // start threads
    xTaskCreate(adc_read_task, "adc_read_task", 4096, NULL, 5, NULL); 
    xTaskCreate(adc_to_i2s_task, "adc_to_i2s_task", 4096, NULL, 5, NULL); 
}