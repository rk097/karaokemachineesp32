#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"

#define FRAME_SIZE 2048

void app_main(void) {
    uint8_t data[FRAME_SIZE]; // buffer in pure bytes
    esp_err_t ret;
    uint32_t retnum;

    // required initialization for ADC continuous driver
    adc_continuous_handle_t handle = NULL;
    adc_continuous_handle_cfg_t adc_init_config = {
        .max_store_buf_size = 8192,
        .conv_frame_size = FRAME_SIZE // in bytes
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_init_config, &handle));

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
    ESP_ERROR_CHECK(adc_continuous_config(handle, &adc_read_config));

    // start ADC continuous sampling
    ESP_ERROR_CHECK(adc_continuous_start(handle));
    while (1) {
        ret = adc_continuous_read(handle, data, FRAME_SIZE, &retnum, 500); // 500 ms for dbg, 30ms actual
        if (ret == ESP_OK) {
            printf("Read %lu bytes\n", (unsigned long)retnum);
            //for (uint32_t i = 0; i < retnum; i += 2) {
                uint16_t sample = (data[0] | (data[0 + 1] << 8)) & 0x0FFF; // lower 12 bits are adc
                printf("%d\n", sample);
            //}
        } else if (ret == ESP_ERR_TIMEOUT) {
            printf("ADC read timeout\n");
        } else {
            printf("ADC read error: %d\n", ret);
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // wait 
    }

    // stop adc 
    ESP_ERROR_CHECK(adc_continuous_stop(handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(handle));
}
