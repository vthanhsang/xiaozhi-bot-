#include <TFT_eSPI.h>
#include "RoboEyesTFT_eSPI.h"
#include <driver/i2s.h>

// ===== TFT =====
TFT_eSPI tft = TFT_eSPI();
TFT_RoboEyes roboEyes(tft, false, 3);


// ===== I2S MIC (I2S0) =====
#define I2S_MIC_PORT  I2S_NUM_0
#define MIC_BCLK      5
#define MIC_LRC       4
#define MIC_DIN       6

// ===== I2S SPK (I2S1) =====
#define I2S_SPK_PORT  I2S_NUM_1
#define SPK_BCLK      15
#define SPK_LRC       16
#define SPK_DOUT      7
#define SPK_SD        46

// ===== AUDIO CONFIG =====
const int SAMPLE_RATE         = 16000;
const int RECORD_DURATION_SEC = 5;
const int AUDIO_BUFFER_SIZE   = RECORD_DURATION_SEC * SAMPLE_RATE * 2; // 16-bit = 2 bytes

int16_t *audio_buffer = NULL;

// ===== TASK =====
TaskHandle_t eyeTaskHandle;

// ===== TIMER =====
unsigned long lastChange = 0;
int moodIndex = 0;

// ================= EYE TASK =================
void changeMood(int i) {
  switch (i) {
    case 0: roboEyes.setMood(DEFAULT); roboEyes.setColors(TFT_WHITE, TFT_BLACK); break;
    case 1: roboEyes.setMood(HAPPY);   roboEyes.setColors(TFT_GREEN, TFT_BLACK); break;
    case 2: roboEyes.setMood(ANGRY);   roboEyes.setColors(TFT_RED,   TFT_BLACK); break;
    case 3: roboEyes.setMood(TIRED);   roboEyes.setColors(TFT_BLUE,  TFT_BLACK); break;
  }
}

void eyeTask(void *pvParameters) {
  while (true) {
    roboEyes.update();
    if (millis() - lastChange > 5000) {
      moodIndex = (moodIndex + 1) % 4;
      changeMood(moodIndex);
      lastChange = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

// ================= RECORD & PLAYBACK =================
void record_and_playback() {
  audio_buffer = (int16_t *)heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  if (audio_buffer == NULL) {
    Serial.println("ERROR: Could not allocate PSRAM!");
    return;
  }

  // --- Ghi âm ---
  vTaskSuspend(eyeTaskHandle);
  roboEyes.setMood(ANGRY);
  roboEyes.setColors(TFT_RED, TFT_BLACK);
  roboEyes.update();

  Serial.printf("Recording %d seconds...\n", RECORD_DURATION_SEC);

  size_t bytes_read = 0;
  i2s_read(I2S_MIC_PORT, audio_buffer, AUDIO_BUFFER_SIZE, &bytes_read, portMAX_DELAY);
  Serial.printf("%d bytes recorded.\n", bytes_read);

  // --- Phát lại ---
  roboEyes.setMood(HAPPY);
  roboEyes.setColors(TFT_GREEN, TFT_BLACK);
  roboEyes.update();

  digitalWrite(SPK_SD, HIGH);
  delay(20);

  size_t bytes_written = 0;
  i2s_write(I2S_SPK_PORT, audio_buffer, bytes_read, &bytes_written, portMAX_DELAY);
  Serial.printf("%d bytes played.\n", bytes_written);

  digitalWrite(SPK_SD, LOW);

  free(audio_buffer);
  audio_buffer = NULL;

  moodIndex = 0;
  changeMood(moodIndex);
  lastChange = millis();
  vTaskResume(eyeTaskHandle);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(SPK_SD, OUTPUT);
  digitalWrite(SPK_SD, LOW);

  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);

  // ===== TFT =====
  tft.init();
  tft.setRotation(0);

  roboEyes.begin(30);
  roboEyes.setColors(TFT_WHITE, TFT_BLACK);
  roboEyes.setAutoblinker(true, 2, 1);
  roboEyes.setIdleMode(true, 4, 0);
  roboEyes.setWidth(45, 45);
  roboEyes.setHeight(45, 45);
  roboEyes.setBorderradius(8, 8);
  roboEyes.setSpacebetween(30);

  if (!psramFound()) {
    Serial.println("PSRAM NOT FOUND!");
  }

  // ===== I2S MIC — ESP-IDF style cho core 3.x =====
  i2s_config_t mic_config = {
    mic_config.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    mic_config.sample_rate          = (uint32_t)SAMPLE_RATE,
    mic_config.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    mic_config.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    mic_config.communication_format = I2S_COMM_FORMAT_STAND_I2S,
    mic_config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    mic_config.dma_buf_count        = 8,
    mic_config.dma_buf_len          = 1024,
    mic_config.use_apll             = false,
    mic_config.tx_desc_auto_clear   = false,
    mic_config.fixed_mclk           = 0
  };

  i2s_pin_config_t mic_pins = {
    mic_pins.mck_io_num   = I2S_PIN_NO_CHANGE,
    mic_pins.bck_io_num   = MIC_BCLK,
    mic_pins.ws_io_num    = MIC_LRC,
    mic_pins.data_out_num = I2S_PIN_NO_CHANGE,
    mic_pins.data_in_num  = MIC_DIN
  };

  ESP_ERROR_CHECK(i2s_driver_install(I2S_MIC_PORT, &mic_config, 0, NULL));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_MIC_PORT, &mic_pins));

  // ===== I2S SPK =====
  i2s_config_t spk_config = {
    spk_config.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    spk_config.sample_rate          = (uint32_t)SAMPLE_RATE,
    spk_config.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    spk_config.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    spk_config.communication_format = I2S_COMM_FORMAT_STAND_I2S,
    spk_config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    spk_config.dma_buf_count        = 8,
    spk_config.dma_buf_len          = 1024,
    spk_config.use_apll             = false,
    spk_config.tx_desc_auto_clear   = true,  // tránh noise khi buffer trống
    spk_config.fixed_mclk           = 0
  };

  i2s_pin_config_t spk_pins = {
    spk_pins.mck_io_num   = I2S_PIN_NO_CHANGE,
    spk_pins.bck_io_num   = SPK_BCLK,
    spk_pins.ws_io_num    = SPK_LRC,
    spk_pins.data_out_num = SPK_DOUT,
    spk_pins.data_in_num  = I2S_PIN_NO_CHANGE
  };

  ESP_ERROR_CHECK(i2s_driver_install(I2S_SPK_PORT, &spk_config, 0, NULL));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_SPK_PORT, &spk_pins));

  delay(100);

  xTaskCreatePinnedToCore(eyeTask, "eyeTask", 4096, NULL, 1, &eyeTaskHandle, 1);

  // Tự động chạy record & playback liên tục
  record_and_playback();
}

// ================= LOOP =================
void loop() {
  record_and_playback();
}