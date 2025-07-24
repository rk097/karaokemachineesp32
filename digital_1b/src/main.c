#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "I2S.h"
#include "utils.h" 
#include "constants.h"
#include "math.h"

// handlers, runtime constants
i2s_chan_handle_t i2s_in_handle = NULL;
i2s_chan_handle_t i2s_out_handle = NULL;
QueueHandle_t i2s_queue = NULL;
const int i2s_delay_ms = 1000*FRAME_SIZE/SAMPLE_RATE; // how long adc_continuous_read should wait for reads
volatile int numbytesread;

// anti cricket stuff.
static const int16_t fir7[4] = { 175, 603, 886, 1024 }; // Q15 coeffs
static int16_t lowpass_delay[7];
static uint8_t idx = 0;

int16_t lowpass_7kHz(int16_t x) { // move to utils..?
    lowpass_delay[idx] = x;

    int32_t acc =  fir7[3] * lowpass_delay[idx]                                 // h3·x[n-3]
                 + fir7[2] * (lowpass_delay[(idx+6)%7] + lowpass_delay[(idx+1)%7])      // h2·(x[n]+x[n-6])
                 + fir7[1] * (lowpass_delay[(idx+5)%7] + lowpass_delay[(idx+2)%7])      // h1· …
                 + fir7[0] * (lowpass_delay[(idx+4)%7] + lowpass_delay[(idx+3)%7]);     // h0· …

    idx = (idx + 1) % 7;
    return (int16_t)(acc >> 15);
}

// read in i2s 
void i2s_read_task(void* param) {
    // int16_t* i2s_output_data = malloc();
    int32_t raw_input_data;
    while(1) {
        if (i2s_channel_read(i2s_in_handle, &raw_input_data, sizeof(raw_input_data), NULL, pdMS_TO_TICKS(500)) == ESP_OK) {
            //for (int i = 0; i < sizeof(raw_input_data)/sizeof(int32_t); i++) {
              //  printf("Data[%d]: %lu\n", i, raw_input_data[i] >> 7);
            //}
            
            int32_t sample24 = raw_input_data << 1 >> 8;
            printf("Data: %ld\n", sample24);
            //printf("Num bytes read: %d\n", numbytesread);
            //vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}

// i2s out task
void i2s_out_task(void *param) {
    int16_t* i2s_data = NULL;
    while (1) {
        if (xQueueReceive(i2s_queue, &i2s_data, portMAX_DELAY) == pdTRUE) {
            if (i2s_data != NULL) {
                // swap bits??
                for (uint16_t i = 0; i < FRAME_SIZE/2; i+=2) {
                    int16_t temp = i2s_data[i+1];
                    i2s_data[i+1] = i2s_data[i];
                    i2s_data[i] = temp;
                }
                i2s_write_once(&i2s_out_handle, i2s_data, FRAME_SIZE/2 * sizeof(int16_t));
                free(i2s_data); // free after writing
            }
        } else {
            printf("Failed to receive i2s data from queue\n");
        }
    }
}

void app_main(void) {
    // queue for i2s data
    i2s_queue = xQueueCreate(6, sizeof(int16_t*)); // store pointers to i2s data buffers
    if (i2s_queue == NULL) {
        printf("Failed to create i2s queue\n");
        return;
    }

    // start I2S
    i2s_in_init(&i2s_in_handle, FRAME_SIZE/sizeof(int32_t), SAMPLE_RATE);
    i2s_out_init(&i2s_out_handle, FRAME_SIZE/sizeof(int16_t), SAMPLE_RATE);
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_in_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_out_handle));
    vTaskDelay(pdMS_TO_TICKS(1000)); // give some time for i2s to start

    // start threads
    xTaskCreate(i2s_read_task, "i2s_read_task", 4096, NULL, 5, NULL); 
    // xTaskCreate(i2s_out_task, "i2s_out_task", 4096, NULL, 5, NULL);
}