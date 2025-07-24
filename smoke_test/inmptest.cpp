#include <Arduino.h>
#include "driver/i2s.h"

// ---------- pin definitions ----------
static const i2s_port_t I2S_PORT = I2S_NUM_0;
static const int PIN_I2S_BCLK = 33;   // SCK  (bit‑clock)
static const int PIN_I2S_WS   = 32;   // LRCK (word‑select)
static const int PIN_I2S_SD   = 34;   // SD   (data from mic)

// ---------- I²S setup ----------
void setupI2SMic(uint32_t sampleRate = 32000)
{
  // 1) Common I²S driver config
  i2s_config_t cfg = {
      .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate          = sampleRate,
      .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,      // 32‑bit slots
      .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,      // mono (matches L/R pin)
      .communication_format = I2S_COMM_FORMAT_I2S,           // Philips/I²S standard
      .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count        = 4,
      .dma_buf_len          = 256,   // each DMA buffer = 256 × 32‑bit samples
      .use_apll             = false,
      .tx_desc_auto_clear   = false,
      .fixed_mclk           = 0
  };

  // 2) Pin mapping
  i2s_pin_config_t pin_cfg = {
      .bck_io_num   = PIN_I2S_BCLK,
      .ws_io_num    = PIN_I2S_WS,
      .data_out_num = -1,          // not used (we’re only receiving)
      .data_in_num  = PIN_I2S_SD
  };

  // 3) Driver install
  ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, nullptr));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pin_cfg));

  // 4) Clear DMA buffers
  i2s_zero_dma_buffer(I2S_PORT);
}

// ---------- Arduino ----------
void setup()
{
  Serial.begin(115200);
  delay(1000);
  setupI2SMic();          // default 48 kHz
  Serial.println("INMP441 smoke‑test started…");
}

void loop()
{
  uint32_t raw32;
  size_t   bytesRead = 0;

  // Read ONE 32‑bit slot (blocking)
  esp_err_t ok = i2s_read(I2S_PORT, &raw32, sizeof(raw32), &bytesRead, portMAX_DELAY);
  if (ok != ESP_OK || bytesRead != sizeof(raw32)) return;

  // Drop the 8 LSB padding bits → 24‑bit signed
  int32_t sample24 = (int32_t)raw32 >> 8;
  // Compensate for INMP441 1‑bit late MSB
  sample24 <<= 1;

  Serial.println(sample24);   // watch values in Serial Plotter
}
