#include "esp_log.h"
#include "freertos/ringbuf.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"

#include "constants.h"
#include "I2S.h"
#include "Bluetooth.h"

#define TAG_MAIN "MAIN"

// globals
static int32_t global_buffer[DMA_BUFFER_COUNT][FRAME_SIZE]; // buffer roll for i2s mic input.
static i2s_chan_handle_t i2s_in_handle = NULL; // i2s mic input stream
static i2s_chan_handle_t i2s_out_handle = NULL; // i2s output stream
static RingbufHandle_t bt_ringbuf = NULL; // bluetooth input bytes
static QueueHandle_t i2s_queue_free = NULL;
static QueueHandle_t i2s_queue_busy = NULL;
static bool bt_playing = false;

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
            byte_data = xRingbufferReceiveUpTo(bt_ringbuf, &item_size, 0, 2*sizeof(int16_t) * FRAME_SIZE);
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
            if (xQueueSend(i2s_queue_free, &i2s_mic_data, portMAX_DELAY) != pdTRUE) {
                printf("Could not return buffer to free queue\n");
            } 
        } else {
            printf("Failed to receive i2s data from queue\n");
        }    

        // write to i2s.
        i2s_channel_write(i2s_out_handle, output_buffer, FRAME_SIZE*sizeof(int16_t)*2, NULL, pdMS_TO_TICKS(20));  
    }
}

void app_main(void)
{       
    // queue for incoming i2s data
    i2s_queue_free = xQueueCreate(DMA_BUFFER_COUNT, sizeof(int32_t*)); // store pointers to i2s data buffers
    if (i2s_queue_free == NULL) {
        ESP_LOGE(TAG_MAIN, "%s free queue create failed", __func__);
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
        ESP_LOGE(TAG_MAIN, "%s busy queue create failed", __func__);
        vQueueDelete(i2s_queue_free);
        return;
    }

    // init i2s
    i2s_init(&i2s_in_handle, &i2s_out_handle, SAMPLE_RATE, FRAME_SIZE, DMA_BUFFER_COUNT);

    ESP_ERROR_CHECK(i2s_channel_enable(i2s_in_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_out_handle));
    ESP_LOGI(TAG_MAIN, "I2S enabled");
    xTaskCreate(i2s_write_task, "i2s_write_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG_MAIN, "I2S Write Task has begun");
    xTaskCreate(i2s_read_task, "i2s_read_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG_MAIN, "I2S Read Task has begun");

    bt_init(&bt_ringbuf, RINGBUFFER_CAPACITY, &bt_playing);
}