#include <TFT_eSPI.h>
#include "RoboEyesTFT_eSPI.h"
#include <driver/i2s.h>

// ===== TFT =====
TFT_eSPI tft = TFT_eSPI();
TFT_RoboEyes roboEyes(tft, false, 3);

// ===== I2S =====
#define I2S_WS   4
#define I2S_SCK  5
#define I2S_SD_IN  6
#define I2S_SD_OUT 7
#define I2S_PORT I2S_NUM_0

// ===== TASK =====
TaskHandle_t audioTaskHandle;
TaskHandle_t eyeTaskHandle;

// ===== TIMER =====
unsigned long lastChange = 0;
int moodIndex = 0;

// ================= AUDIO TASK =================
void audioTask(void *pvParameters) {
  int32_t raw[256];   // mic 32-bit
  int16_t out[256];   // loa 16-bit

  size_t bytes_read = 0;
  size_t bytes_written = 0;

  while (true) {
    // ===== đọc mic (BLOCKING) =====
    i2s_read(I2S_PORT, raw, sizeof(raw), &bytes_read, portMAX_DELAY);

    int samples = bytes_read / 4;

    // ===== convert chuẩn 32bit -> 16bit =====
    for (int i = 0; i < samples; i++) {
      int32_t s = raw[i];

      // lọc nhiễu nhỏ (noise gate)
      if (abs(s) < 500) s = 0;

      // shift chuẩn (QUAN TRỌNG)
      out[i] = s >> 16;
    }

    // ===== phát loa =====
    if (samples > 0) {
      i2s_write(I2S_PORT, out, samples * 2, &bytes_written, portMAX_DELAY);
    }
  }
}

// ================= EYE =================
void changeMood(int i) {
  switch(i) {
    case 0:
      roboEyes.setMood(DEFAULT);
      roboEyes.setColors(TFT_WHITE, TFT_BLACK);
      break;
    case 1:
      roboEyes.setMood(HAPPY);
      roboEyes.setColors(TFT_GREEN, TFT_BLACK);
      break;
    case 2:
      roboEyes.setMood(ANGRY);
      roboEyes.setColors(TFT_RED, TFT_BLACK);
      break;
    case 3:
      roboEyes.setMood(TIRED);
      roboEyes.setColors(TFT_BLUE, TFT_BLACK);
      break;
  }
}

void eyeTask(void *pvParameters) {
  while (true) {
    roboEyes.update();

    if (millis() - lastChange > 5000) {
      moodIndex++;
      if (moodIndex > 3) moodIndex = 0;

      changeMood(moodIndex);
      lastChange = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(30)); // ~30 FPS
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  // ===== BACKLIGHT =====
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);

  // ===== TFT =====
  tft.init();
  tft.setRotation(0);

  // ===== RoboEyes =====
  roboEyes.begin(30);
  roboEyes.setColors(TFT_WHITE, TFT_BLACK);
  roboEyes.setAutoblinker(true, 2, 1);
  roboEyes.setIdleMode(true, 4, 0);

  roboEyes.setWidth(45, 45);
  roboEyes.setHeight(45, 45);
  roboEyes.setBorderradius(8, 8);
  roboEyes.setSpacebetween(30);

  // ===== I2S CONFIG =====
  i2s_config_t i2s_config;
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  i2s_config.sample_rate = 16000;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  i2s_config.intr_alloc_flags = 0;
  i2s_config.dma_buf_count = 8;
  i2s_config.dma_buf_len = 64;
  i2s_config.use_apll = false;
  i2s_config.tx_desc_auto_clear = true;
  i2s_config.fixed_mclk = 0;

  i2s_pin_config_t pin_config;
  pin_config.bck_io_num = I2S_SCK;
  pin_config.ws_io_num = I2S_WS;
  pin_config.data_out_num = I2S_SD_OUT;
  pin_config.data_in_num = I2S_SD_IN;
  pin_config.mck_io_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);

  // ===== TASK =====
  xTaskCreatePinnedToCore(audioTask, "audioTask", 4096, NULL, 2, &audioTaskHandle, 0);
  xTaskCreatePinnedToCore(eyeTask, "eyeTask", 4096, NULL, 1, &eyeTaskHandle, 1);
}

// ================= LOOP =================
void loop() {
}