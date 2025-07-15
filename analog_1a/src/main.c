#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "driver/i2s_std.h"
#include "helpers.h" // find_idle_value, convert_adc_sample, scale_adc_to_i2s
#include "globals.h" // FRAME_SIZE

// initialize the adc continuous driver on ADC1 channel 0 (GPIO36)
void adc_init(adc_continuous_handle_t* handle_ptr) {
    adc_continuous_handle_cfg_t adc_init_config = {
        .max_store_buf_size = 8192,
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
}

// fills data from adc stream once.
// will fail if init & start not called first
void adc_read_once(adc_continuous_handle_t* handle_ptr, uint8_t* data, bool dbg) {
    esp_err_t ret;
    uint32_t delay, retnum;
    if (dbg) delay = 100; else delay = 30; // ms to wait for data, 30ms is enough for 44.1kHz
    ret = adc_continuous_read(*handle_ptr, data, FRAME_SIZE, &retnum, delay); // 500 ms for dbg, 30ms actual
    
    if (ret == ESP_OK) {
        if (dbg) {
            printf("Read %lu bytes\n", (unsigned long)retnum);
            //for (uint32_t i = 0; i < *retnum; i += 2) { // print one value vs continuous stream
                uint16_t sample = convert_adc_sample(data[0], data[1]); // lower 12 bits are adc
                printf("%d\n", sample);
            //}
            vTaskDelay(delay);
        }
    } else if (ret == ESP_ERR_TIMEOUT) {
        printf("ADC read timeout\n");
    } else {
        printf("ADC read error: %d\n", ret);
    }
}

void app_main(void) {
    uint8_t data[FRAME_SIZE]; // buffer in pure bytes
    adc_continuous_handle_t handle = NULL;
    uint16_t idle_adc_val;

    adc_init(&handle);

    // start ADC continuous sampling
    ESP_ERROR_CHECK(adc_continuous_start(handle));

    // find idle value 
    adc_read_once(&handle, data, false); 
    idle_adc_val = find_idle_value(data); // find idle value for i2s scaling

    while (1) {
        adc_read_once(&handle, data, true);
    }

    // stop adc 
    ESP_ERROR_CHECK(adc_continuous_stop(handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(handle));
}
