#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <driver/i2s.h>

// ===== TFT =====
#define TFT_CS   3
#define TFT_DC   10
#define TFT_RST  9
#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_BLK  13

SPIClass spi = SPIClass(FSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&spi, TFT_CS, TFT_DC, TFT_RST);

// ===== I2S =====
#define I2S_WS   4
#define I2S_SCK  5
#define I2S_SD_IN  6   // mic
#define I2S_SD_OUT 7   // loa

#define I2S_PORT I2S_NUM_0

void setup() {
  Serial.begin(115200);

  // ===== PWM BLK (core 3.x) =====
  ledcAttach(TFT_BLK, 5000, 8);
  ledcWrite(TFT_BLK, 200);

  // ===== TFT =====
  spi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.init(240, 240);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(3);
  tft.setCursor(20, 100);
  tft.setTextColor(ST77XX_WHITE);
  tft.print("Audio OK");

  // ===== I2S CONFIG (core 3.x SAFE) =====
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

  // ===== PIN CONFIG =====
  i2s_pin_config_t pin_config;
  pin_config.bck_io_num = I2S_SCK;
  pin_config.ws_io_num = I2S_WS;
  pin_config.data_out_num = I2S_SD_OUT;
  pin_config.data_in_num = I2S_SD_IN;
  pin_config.mck_io_num = I2S_PIN_NO_CHANGE;

  // ===== START I2S =====
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}

void loop() {
  int32_t buffer[256];
  size_t bytes_read, bytes_written;

  // đọc mic
  i2s_read(I2S_PORT, &buffer, sizeof(buffer), &bytes_read, portMAX_DELAY);

  // phát loa
  i2s_write(I2S_PORT, &buffer, bytes_read, &bytes_written, portMAX_DELAY);
}