#ifndef I2S_H
#define I2S_H

#include "driver/i2s_std.h"

// initalizes i2s output to gpio pins 26,25,22
void i2s_init(i2s_chan_handle_t* chan_handle_ptr);

// writes frame_size i2s samples to the DMA output buffer
esp_err_t i2s_write_once(i2s_chan_handle_t* chan_handle_ptr, int16_t* data, size_t frame_size);

#endif