#include <WiFi.h>
#include <WebSocketsClient.h>
#include <TFT_eSPI.h>
#include "RoboEyesTFT_eSPI.h"
#include <driver/i2s.h>

// ===== WIFI =====
const char* ssid     = "sang";
const char* password = "11111111";

// ===== WEBSOCKET =====
WebSocketsClient webSocket;
bool isConnected = false;

// ===== TFT =====
TFT_eSPI tft = TFT_eSPI();
TFT_RoboEyes roboEyes(tft, false, 3);

// ===== I2S MIC =====
#define I2S_MIC_PORT  I2S_NUM_0
#define MIC_BCLK      5
#define MIC_LRC       4
#define MIC_DIN       6

// ===== I2S SPK =====
#define I2S_SPK_PORT  I2S_NUM_1
#define SPK_BCLK      15
#define SPK_LRC       16
#define SPK_DOUT      7
#define SPK_SD        46

// ===== AUDIO =====
const int SAMPLE_RATE         = 16000;
const int RECORD_DURATION_SEC = 3;
const int AUDIO_BUFFER_SIZE   = RECORD_DURATION_SEC * SAMPLE_RATE * 2;

int16_t* audio_buffer = NULL;

// ===== TASK =====
TaskHandle_t eyeTaskHandle;
TaskHandle_t wsTaskHandle;
TaskHandle_t audioTaskHandle;

// ===== CONTROL FLAG (THAY SUSPEND) =====
bool pauseEye = false;

// ===== TIMER =====
unsigned long lastChange = 0;
int moodIndex = 0;

// ================= MOOD =================
void changeMood(int i) {
  switch (i) {
    case 0: roboEyes.setMood(DEFAULT); break;
    case 1: roboEyes.setMood(HAPPY);   break;
    case 2: roboEyes.setMood(ANGRY);   break;
    case 3: roboEyes.setMood(TIRED);   break;
  }
}

// ================= EYE TASK =================
void eyeTask(void* pvParameters) {
  while (true) {

    if (!pauseEye) {
      roboEyes.update();

      if (millis() - lastChange > 5000) {
        moodIndex = (moodIndex + 1) % 4;
        changeMood(moodIndex);
        lastChange = millis();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

// ================= WS TASK =================
void webSocketTask(void* pvParameters) {
  while (true) {
    webSocket.loop();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ================= AUDIO TASK =================
void audioTask(void* pvParameters) {
  while (true) {

    if (!isConnected) {
      Serial.println("[WS] Waiting...");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }

    record_and_send();
    vTaskDelay(15000 / portTICK_PERIOD_MS);
  }
}

// ================= WS EVENT =================
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      isConnected = true;
      Serial.println("[WS] Connected!");
      break;

    case WStype_DISCONNECTED:
      isConnected = false;
      Serial.println("[WS] Disconnected!");
      break;

    case WStype_TEXT:
      Serial.printf("[WS] Text: %s\n", payload);
      break;

    case WStype_BIN:
      Serial.printf("[WS] Audio received: %d bytes\n", length);
      break;
  }
}

// ================= RECORD & SEND =================
void record_and_send() {

  audio_buffer = (int16_t*)heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  if (!audio_buffer) {
    Serial.println("[ERROR] PSRAM alloc failed!");
    return;
  }

  // --- Pause eye bằng FLAG ---
  pauseEye = true;

  roboEyes.setMood(ANGRY);
  roboEyes.update();

  Serial.println("[REC] Recording...");

  size_t bytes_read = 0;

while (bytes_read < AUDIO_BUFFER_SIZE) {

  size_t remaining = AUDIO_BUFFER_SIZE - bytes_read;
  size_t to_read = remaining > 1024 ? 1024 : remaining;

  size_t chunk = 0;

  i2s_read(I2S_MIC_PORT,
           (uint8_t*)audio_buffer + bytes_read,
           to_read,
           &chunk,
           portMAX_DELAY);

  bytes_read += chunk;

  vTaskDelay(1);
}

  Serial.printf("[REC] Done: %d bytes\n", bytes_read);

  roboEyes.setMood(HAPPY);
  roboEyes.update();

  bool ok = webSocket.sendBIN((uint8_t*)audio_buffer, bytes_read);

  if (ok) Serial.println("[WS] Sent OK");
  else    Serial.println("[WS] Send FAIL");

  free(audio_buffer);
  audio_buffer = NULL;

  pauseEye = false;

  moodIndex = 0;
  changeMood(moodIndex);
  lastChange = millis();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(SPK_SD, OUTPUT);
  digitalWrite(SPK_SD, LOW);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[WiFi] Connected!");
  Serial.println(WiFi.localIP());

  webSocket.begin("192.168.233.20", 8000, "/ws/audio");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);

  // TFT
  tft.init();
  tft.setRotation(0);
  roboEyes.begin(30);

  // I2S MIC
  i2s_config_t mic_config = {
    mic_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    mic_config.sample_rate = SAMPLE_RATE,
    mic_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    mic_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    mic_config.communication_format = I2S_COMM_FORMAT_STAND_I2S,
    mic_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    mic_config.dma_buf_count = 8,
    mic_config.dma_buf_len = 1024
  };

  i2s_pin_config_t mic_pins = {
    mic_pins.bck_io_num = MIC_BCLK,
    mic_pins.ws_io_num = MIC_LRC,
    mic_pins.data_in_num = MIC_DIN,
    mic_pins.data_out_num = I2S_PIN_NO_CHANGE
  };
  

  i2s_driver_install(I2S_MIC_PORT, &mic_config, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &mic_pins);

  // TASKS
  xTaskCreatePinnedToCore(eyeTask,       "eyeTask", 4096, NULL, 1, &eyeTaskHandle, 1);
  xTaskCreatePinnedToCore(webSocketTask, "wsTask", 4096, NULL, 2, &wsTaskHandle, 0);
  xTaskCreatePinnedToCore(audioTask,     "audioTask", 8192, NULL, 1, &audioTaskHandle, 1);
}

// ================= LOOP =================
void loop() {}
