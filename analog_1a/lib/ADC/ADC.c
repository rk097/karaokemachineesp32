#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "ADC.h"
#include "utils.h"

void adc_init(adc_continuous_handle_t* handle_ptr, uint32_t frame_size) {
    adc_continuous_handle_cfg_t adc_init_config = {
        .max_store_buf_size = frame_size * 4,
        .conv_frame_size = frame_size // in bytes
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

esp_err_t adc_read_once(adc_continuous_handle_t* handle_ptr, uint8_t* data, uint32_t frame_size, bool dbg) {
    esp_err_t ret;
    uint32_t delay, retnum;
    if (dbg) delay = 500; else delay = 30; // ms to wait for data, 30ms is enough for 44.1kHz
    ret = adc_continuous_read(*handle_ptr, data, frame_size, &retnum, delay); // 500 ms for dbg, 30ms actual
    
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