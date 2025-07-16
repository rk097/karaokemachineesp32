#ifndef HELPERS_H
#define HELPERS_H

// functions that don't require state or library, like math functions

#include <stdio.h>

uint16_t find_idle_value(uint8_t* data); // finds idle value in adc data, simple average of samples
uint16_t convert_adc_sample(uint8_t val1, uint8_t val2); // convert 2 bytes of adc data to 12-bit sample
int16_t scale_adc_to_i2s(uint16_t adc_sample, uint16_t idle_adc_val); // scale adc sample to i2s 16-bit signed value

#endif