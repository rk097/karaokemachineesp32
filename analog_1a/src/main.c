#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ADC.h"
#include "driver/i2s_std.h"
#include "utils.h" 
#include "constants.h"

// handlers
adc_continuous_handle_t adc_handle = NULL;
QueueHandle_t adc_queue = NULL;
QueueHandle_t i2s_queue = NULL;

// task to read adc data continuously
void adc_read_task(void *param) {
    while (1) {
        uint8_t* data = malloc(FRAME_SIZE); // buffer in pure bytes
        if (adc_read_once(&adc_handle, data, FRAME_SIZE, false) == ESP_OK) { // safeguard against sending garbage into queue
            if(xQueueSend(adc_queue, &data, pdMS_TO_TICKS(100)) != pdTRUE) { // send data to queue
                printf("Failed to send adc data to queue\n");
                free(data); // free if send failed. needs to freed on receiver end if send succeeds
            } else {
                printf("ADC data sent to queue\n");
            }
        }
    }
}

// adc to i2s processing task
void adc_to_i2s_task(void *param) {
    uint8_t* data = malloc(FRAME_SIZE); 
    uint16_t idle_adc_val;
    // find idle value 
    while(adc_read_once(&adc_handle, data, FRAME_SIZE, false) != ESP_OK) {}; // make sure we have valid data for idle calibration
    idle_adc_val = find_idle_value(data, FRAME_SIZE); // find idle value for i2s scaling
    free(data); // free after finding idle value
    data = NULL; // reset pointer

    while (1) {
        if (xQueueReceive(adc_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (data != NULL) {
                printf("Received adc data, processing...\n");
                // scale to i2s format
                int16_t* i2s_data = malloc(FRAME_SIZE/2 * sizeof(int16_t)); // allocate memory for i2s data
                for (uint16_t i = 0; i < FRAME_SIZE; i += 2) {
                    uint16_t sample = convert_adc_sample(data[i], data[i + 1]); // lower 12 bits are adc
                    i2s_data[i/2] = scale_adc_to_i2s(sample, idle_adc_val); // 2048 adc half-samples filled into 1024 i2s samples
                    // For example: i2s_write(I2S_NUM_0, &scaled_value, sizeof(scaled_value), &bytes_written, portMAX_DELAY);
                }
                free(data); // free after processing
                if(xQueueSend(i2s_queue, &i2s_data, pdMS_TO_TICKS(100)) != pdTRUE) {
                    printf("Failed to send i2s data to queue\n");
                    free(i2s_data); // free if send failed
                } 
            }
        } else {
            printf("Failed to receive adc data from queue\n");
        }
    }
}

// i2s out task

void app_main(void) {
    // queue for adc readings
    adc_queue = xQueueCreate(6, sizeof(uint8_t*)); // store pointers to data buffers to prevent unnecessary copies
    if (adc_queue == NULL) {
        printf("Failed to create adc queue\n");
        return;
    }

    // queue for i2s data
    i2s_queue = xQueueCreate(6, sizeof(int16_t*)); // store pointers to i2s data buffers
    if (i2s_queue == NULL) {
        printf("Failed to create i2s queue\n");
        vQueueDelete(adc_queue); // cleanup adc queue
        return;
    }

    // start ADC continuous sampling
    adc_init(&adc_handle, FRAME_SIZE);
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
    vTaskDelay(pdMS_TO_TICKS(1000)); // give some time for adc to start

    // start threads
    xTaskCreate(adc_read_task, "adc_read_task", 4096, NULL, 5, NULL); 
    xTaskCreate(adc_to_i2s_task, "adc_to_i2s_task", 4096, NULL, 5, NULL); 
}