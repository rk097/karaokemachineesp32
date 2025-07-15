#include <stdio.h>
#include "helpers.h"
#include "globals.h"

// finds idle value in adc data
uint16_t find_idle_value(uint8_t* data) {
    // simple average to find idle value
    uint32_t sum = 0;
    for (uint32_t i = 0; i < FRAME_SIZE; i += 2) {
        uint16_t sample = convert_adc_sample(data[i], data[i + 1]); 
        sum += sample;
    }
    return (sum / (FRAME_SIZE / 2)); 
}

// convert 2 bytes of adc data to 12-bit sample
uint16_t convert_adc_sample(uint8_t val1, uint8_t val2) {
    return (val1 | (val2 << 8)) & 0x0FFF; // lower 12 bits are adc
}