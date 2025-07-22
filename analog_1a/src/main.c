#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ADC.h"
#include "I2S.h"
#include "utils.h" 
#include "constants.h"
#include "math.h"

// handlers, runtime constants
adc_continuous_handle_t adc_handle = NULL;
i2s_chan_handle_t i2s_out_handle = NULL;
QueueHandle_t adc_queue = NULL;
QueueHandle_t i2s_queue = NULL;
const int adc_delay_ms = 1000*FRAME_SIZE/SAMPLE_RATE; // how long adc_continuous_read should wait for reads
volatile int adc_bytes_cnt = 0;
volatile int i2s_bytes_cnt = 0;

// anti cricket stuff.
static const int16_t fir7[4] = { 175, 603, 886, 1024 }; // Q15 coeffs
static int16_t lowpass_delay[7];
static uint8_t idx = 0;

int16_t lowpass_7kHz(int16_t x) {
    lowpass_delay[idx] = x;

    int32_t acc =  fir7[3] * lowpass_delay[idx]                                 // h3·x[n-3]
                 + fir7[2] * (lowpass_delay[(idx+6)%7] + lowpass_delay[(idx+1)%7])      // h2·(x[n]+x[n-6])
                 + fir7[1] * (lowpass_delay[(idx+5)%7] + lowpass_delay[(idx+2)%7])      // h1· …
                 + fir7[0] * (lowpass_delay[(idx+4)%7] + lowpass_delay[(idx+3)%7]);     // h0· …

    idx = (idx + 1) % 7;
    return (int16_t)(acc >> 15);
}


// task to read adc data continuously
void adc_read_task(void *param) {
    while (1) {
        uint8_t* data = malloc(FRAME_SIZE); // buffer in pure bytes
        if (adc_read_once(&adc_handle, data, FRAME_SIZE, adc_delay_ms) == ESP_OK) { // safeguard against sending garbage into queue
            if(xQueueSend(adc_queue, &data, pdMS_TO_TICKS(100)) != pdTRUE) { // send data to queue
                printf("Failed to send adc data to queue\n");
                free(data); // free if send failed. needs to freed on receiver end if send succeeds
            } 
        }
    }
}

// adc to i2s processing task
void adc_to_i2s_task(void *param) {
    uint8_t* adc_data = malloc(FRAME_SIZE); 
    uint16_t idle_adc_val;
    // find idle value 
    while(adc_read_once(&adc_handle, adc_data, FRAME_SIZE, adc_delay_ms) != ESP_OK) {}; // make sure we have valid data for idle calibration
    idle_adc_val = find_idle_value(adc_data, FRAME_SIZE); // find idle value for i2s scaling
    free(adc_data); // free after finding idle value
    adc_data = NULL; // reset pointer

    while (1) {
        if (xQueueReceive(adc_queue, &adc_data, portMAX_DELAY) == pdTRUE) {
            if (adc_data != NULL) {
                // scale to i2s format
                int16_t* i2s_data = malloc(FRAME_SIZE/2 * sizeof(int16_t)); // allocate memory for i2s data
                for (uint16_t i = 0; i < FRAME_SIZE; i += 2) {
                    uint16_t sample = convert_adc_sample(adc_data[i], adc_data[i + 1]); // lower 12 bits are adc
                    //printf("ADC Sample: %d\n", sample);
                    i2s_data[i/2] = lowpass_7kHz(scale_adc_to_i2s(sample, idle_adc_val)); // 2048 adc half-samples filled into 1024 i2s samples
                    if (abs(i2s_data[i/2]) < 500) i2s_data[i/2] = 0; // noise gate
                    //printf("I2S Converted & filtered: %d\n", i2s_data[i/2]);
                }
                free(adc_data); // free after processing
                if(xQueueSend(i2s_queue, &i2s_data, pdMS_TO_TICKS(100)) != pdTRUE) {
                    printf("Failed to send i2s data to queue\n");
                    free(i2s_data); // free if send failed
                } 
            }
        } 
    }
}

// i2s out task
void i2s_out_task(void *param) {
    int16_t* i2s_data = NULL;
    while (1) {
        if (xQueueReceive(i2s_queue, &i2s_data, portMAX_DELAY) == pdTRUE) {
            if (i2s_data != NULL) {
                // reduce amplification and swap bits??
                
                for (uint16_t i = 0; i < FRAME_SIZE/2; i+=2) {
                    int16_t temp = i2s_data[i+1];
                    i2s_data[i+1] = i2s_data[i]/2;
                    i2s_data[i] = temp/2;
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

    // start ADC continuous sampling and I2S
    adc_init(&adc_handle, FRAME_SIZE, SAMPLE_RATE);
    i2s_init(&i2s_out_handle, SAMPLE_RATE);
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_out_handle));
    vTaskDelay(pdMS_TO_TICKS(1000)); // give some time for adc to start

    // start threads
    xTaskCreate(adc_read_task, "adc_read_task", 4096, NULL, 5, NULL); 

    xTaskCreate(adc_to_i2s_task, "adc_to_i2s_task", 4096, NULL, 5, NULL); 
    

    xTaskCreate(i2s_out_task, "i2s_out_task", 4096, NULL, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(1000));
    printf("ADC bytes in one second: %d\n", adc_bytes_cnt);
    printf("I2S bytes in one second: %d\n", i2s_bytes_cnt);

}