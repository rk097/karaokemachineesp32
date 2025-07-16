#include <stdio.h>
#include "utils.h"

uint16_t find_idle_value(uint8_t* data, uint32_t frame_size) {
    // simple average to find idle value
    uint32_t sum = 0;
    for (uint32_t i = 0; i < frame_size; i += 2) {
        uint16_t sample = convert_adc_sample(data[i], data[i + 1]); 
        sum += sample;
    }
    return (sum / (frame_size / 2)); 
}

uint16_t convert_adc_sample(uint8_t val1, uint8_t val2) {
    return (val1 | (val2 << 8)) & 0x0FFF; // lower 12 bits are adc
}

int16_t scale_adc_to_i2s(uint16_t adc_sample, uint16_t idle_adc_val) {
    // scale to 16-bit signed value
    int32_t diff = adc_sample - idle_adc_val;
    int32_t scaled;
    if (diff >= 0) {
        // Scale positive side
        scaled = diff * 32767 / (4095 - idle_adc_val); // idle_value...4095 is positive range
    } else {
        // Scale negative side
        scaled = diff * 32768 / idle_adc_val; // 0...idle_value is negative range
    }

    return (int16_t)scaled;
}