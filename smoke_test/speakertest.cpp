#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

#define I2S_NUM       I2S_NUM_0
#define I2S_BCLK      26
#define I2S_LRCLK     25
#define I2S_DOUT      22

void setupI2S() {
  const i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  const i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM, &config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pins);
}

void playTone(float freq) {
  const int sampleRate = 44100;
  const int amplitude = 1000;
  const int samples = 256;
  int16_t buffer[samples];

  float phase = 0.0f;
  float phaseIncrement = 2.0f * M_PI * freq / sampleRate;

  while (true) {
    for (int i = 0; i < samples; i++) {
      buffer[i] = (int16_t)(amplitude * sinf(phase));
      phase += phaseIncrement;
      if (phase >= 2.0f * M_PI)
        phase -= 2.0f * M_PI;
    }

    size_t bytesWritten;
    i2s_write(I2S_NUM, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
  }
}

void setup() {
  Serial.begin(115200);
  setupI2S();
  delay(100);
  playTone(440.0);  // A4 test tone
}

void loop() {
  // Not used
}
