#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "I2S.h"
#include "driver/i2s_std.h"

extern volatile int numbytesread;

void i2s_in_init(i2s_chan_handle_t* chan_handle_ptr, uint32_t frame_size, uint32_t sample_rate) {
    // initialize i2s channel and i2s settings
    i2s_chan_config_t i2s_in_chan_config = { 
        .id = I2S_NUM_AUTO, 
        .role = I2S_ROLE_MASTER, 
        .dma_desc_num = 4, 
        .dma_frame_num = 256, 
        .auto_clear_after_cb = false, 
        .auto_clear_before_cb = false,
        .allow_pd = false, 
        .intr_priority = 0, 
    };
    ESP_ERROR_CHECK(i2s_new_channel(&i2s_in_chan_config, NULL, chan_handle_ptr));
    
    i2s_std_config_t i2s_in_config = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // some codecs may require mclk signal, this example doesn't need it
            .bclk = GPIO_NUM_33,
            .ws   = GPIO_NUM_32,
            .dout = I2S_GPIO_UNUSED,
            .din  = GPIO_NUM_34,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*chan_handle_ptr, &i2s_in_config));
    printf("I2S input driver initialized\n");
}

void i2s_out_init(i2s_chan_handle_t* chan_handle_ptr, uint32_t frame_size, uint32_t sample_rate) {
    // initialize i2s channel and i2s settings
    i2s_chan_config_t i2s_out_chan_config = { 
        .id = I2S_NUM_AUTO, 
        .role = I2S_ROLE_MASTER, 
        .dma_desc_num = 4, 
        .dma_frame_num = frame_size, 
        .auto_clear_after_cb = false, 
        .auto_clear_before_cb = false,
        .allow_pd = false, 
        .intr_priority = 0, 
    };
    ESP_ERROR_CHECK(i2s_new_channel(&i2s_out_chan_config, chan_handle_ptr, NULL));

    i2s_std_config_t i2s_out_config = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,    // some codecs may require mclk signal, this example doesn't need it
            .bclk = GPIO_NUM_26,
            .ws   = GPIO_NUM_25,
            .dout = GPIO_NUM_22,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*chan_handle_ptr, &i2s_out_config));
    printf("I2S output driver initialized\n");
}

esp_err_t i2s_read_once(i2s_chan_handle_t* chan_handle_ptr, int32_t* data, size_t frame_size, int32_t delay) {
    size_t bytes_read;
    // read from i2s
    esp_err_t ret = i2s_channel_read(*chan_handle_ptr, data, frame_size, &bytes_read, pdMS_TO_TICKS(delay));
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_TIMEOUT) printf("I2S read timeout\n");
        else printf("I2S read error: %d\n", ret);
        numbytesread = -1;
    } else {
        numbytesread = bytes_read;
    }
    return ret;
}

esp_err_t i2s_write_once(i2s_chan_handle_t* chan_handle_ptr, int16_t* data, size_t frame_size) {
    size_t bytes_written;
    // write to i2s
    esp_err_t ret = i2s_channel_write(*chan_handle_ptr, data, frame_size, &bytes_written, portMAX_DELAY);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_TIMEOUT) printf("I2S write timeout\n");
        else printf("I2S write error: %d\n", ret);
    } 
    return ret;
}