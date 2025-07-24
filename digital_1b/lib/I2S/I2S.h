#ifndef I2S_H
#define I2S_H

#include "driver/i2s_std.h"

// initalizes i2s input to gpio pins 33,32,34
void i2s_in_init(i2s_chan_handle_t* chan_handle_ptr, uint32_t frame_size, uint32_t sample_rate);

// initalizes i2s output to gpio pins 26,25,22
void i2s_out_init(i2s_chan_handle_t* chan_handle_ptr, uint32_t frame_size, uint32_t sample_rate);

// writes frame_size bytes to the DMA output buffer
esp_err_t i2s_write_once(i2s_chan_handle_t* chan_handle_ptr, int16_t* data, size_t frame_size);

// reads frame_size bytes from DMA over i2s
esp_err_t i2s_read_once(i2s_chan_handle_t* chan_handle_ptr, int32_t* data, size_t frame_size, int32_t delay);

#endif