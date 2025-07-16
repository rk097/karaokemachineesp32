#ifndef ADC_H
#define ADC_H

#include "esp_adc/adc_continuous.h"

// initialize the adc continuous driver on ADC1 channel 0 (GPIO36)
void adc_init(adc_continuous_handle_t* handle_ptr, uint32_t frame_size);

// fills data from adc stream once.
// will fail if init & start not called first
esp_err_t adc_read_once(adc_continuous_handle_t* handle_ptr, uint8_t* data, uint32_t frame_size, bool dbg);

#endif