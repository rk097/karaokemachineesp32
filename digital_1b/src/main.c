#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "I2S.h"
#include "utils.h" 
#include "constants.h"
#include "math.h"

// global buffer
static int32_t global_buffer[DMA_BUFFER_COUNT][FRAME_SIZE];

// handlers, runtime constants
i2s_chan_handle_t i2s_in_handle = NULL;
i2s_chan_handle_t i2s_out_handle = NULL;
QueueHandle_t i2s_queue_free = NULL;
QueueHandle_t i2s_queue_busy = NULL;

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
    int32_t* i2s_data = NULL;
    int16_t output_buffer[FRAME_SIZE*2];
    while (1) {
        if (xQueueReceive(i2s_queue_busy, &i2s_data, portMAX_DELAY) == pdTRUE) {
            if (i2s_data != NULL) {
                for (int i = 0; i < FRAME_SIZE*2; i+=2) {
                    output_buffer[i] = (int16_t)(i2s_data[i / 2] >> 16);
                    output_buffer[i+1] = output_buffer[i];
                }
                i2s_write_once(&i2s_out_handle, output_buffer, FRAME_SIZE);
            }
            if (xQueueSend(i2s_queue_free, &i2s_data, portMAX_DELAY) != pdTRUE) {
                printf("Could not return buffer to free queue\n");
            } 
        } else {
            printf("Failed to receive i2s data from queue\n");
        }
    }
}

void app_main(void) {
    // queue for incoming i2s data
    i2s_queue_free = xQueueCreate(DMA_BUFFER_COUNT, sizeof(int32_t*)); // store pointers to i2s data buffers
    if (i2s_queue_free == NULL) {
        printf("Failed to create i2s input queue\n");
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
        printf("Failed to create i2s output queue\n");
        vQueueDelete(i2s_queue_free);
        return;
    }

    // start I2S
    i2s_init(&i2s_in_handle, &i2s_out_handle, SAMPLE_RATE, FRAME_SIZE, DMA_BUFFER_COUNT);
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_in_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_out_handle));
    vTaskDelay(pdMS_TO_TICKS(1000)); // give some time for i2s to start

    printf("I2S fully initialized\n");

    // start threads
    xTaskCreate(i2s_read_task, "i2s_read_task", 4096, NULL, 5, NULL); 
    xTaskCreate(i2s_write_task, "i2s_write_task", 4096, NULL, 5, NULL);
}