#include <driver/i2s.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

// ===== Config =====
const char* WIFI_SSID = "sang";
const char* WIFI_PASS = "sang201104";
const char* WS_HOST   = "192.168.114.20";
const uint16_t WS_PORT = 8000;
const char* WS_PATH   = "/ws/audio";

#define I2S_SD       6
#define I2S_WS       4
#define I2S_SCK      5
#define I2S_BCLK_SPK 15
#define I2S_LRC_SPK  16
#define I2S_DOUT_SPK 7
#define I2S_SD_SPK   46
#define I2S_PORT_RX  I2S_NUM_0
#define I2S_PORT_TX  I2S_NUM_1

#define SAMPLE_RATE      16000
#define RECORD_SECONDS   3
#define MIC_CHUNK        512
#define SEND_BUF_SIZE    (SAMPLE_RATE * 2 * RECORD_SECONDS)
#define PLAY_CHUNK_SIZE  2048

// ===== RTOS objects =====
// Queue gửi chunk audio từ PlayTask
typedef struct {
  uint8_t* data;
  size_t   len;
  bool     isEnd;   // true = kết thúc stream
} AudioChunk;

QueueHandle_t   playQueue;         // WSTask → PlayTask
EventGroupHandle_t stateEvent;

#define EV_LISTENING  BIT0
#define EV_WAITING    BIT1
#define EV_PLAYING    BIT2

WebSocketsClient ws;
bool wsConnected = false;

// ===== Helpers =====
bool hasVoice(int16_t* buf, size_t samples, int16_t thr = 500) {
  for (size_t i = 0; i < samples; i++)
    if (abs(buf[i]) > thr) return true;
  return false;
}

// ===== WebSocket callback (chạy trong WSTask) =====
void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[WS] Connected");
      wsConnected = true;
      xEventGroupSetBits(stateEvent, EV_LISTENING);
      xEventGroupClearBits(stateEvent, EV_WAITING | EV_PLAYING);
      break;

    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected");
      wsConnected = false;
      xEventGroupClearBits(stateEvent, EV_WAITING | EV_PLAYING);
      xEventGroupSetBits(stateEvent, EV_LISTENING);
      break;

    // ===== WSTask — fix: set PLAYING khi nhận chunk đầu =====
      case WStype_BIN: {
        if (length == 0) {
          AudioChunk end = { nullptr, 0, true };
          xQueueSend(playQueue, &end, portMAX_DELAY);
          Serial.println("[WS] Audio END signal sent");
          break;
        }

        uint8_t* buf = (uint8_t*)malloc(length);
        if (!buf) { Serial.println("[WS] malloc fail!"); break; }
        memcpy(buf, payload, length);

        AudioChunk chunk = { buf, length, false };
        if (xQueueSend(playQueue, &chunk, pdMS_TO_TICKS(500)) != pdTRUE) {
          Serial.println("[WS] Queue full, drop");
          free(buf);
        } else {
          // Chỉ set PLAYING khi queue nhận được — không phải trước
          EventBits_t bits = xEventGroupGetBits(stateEvent);
          if (!(bits & EV_PLAYING)) {
            xEventGroupSetBits(stateEvent, EV_PLAYING);
            xEventGroupClearBits(stateEvent, EV_WAITING | EV_LISTENING);
            Serial.println("[WS] → PLAYING");
          }
        }
        break;
      }

    default: break;
  }
}

// ===== MicTask — thêm check PLAYING =====
void micTask(void* arg) {
  static uint8_t  sendBuf[SEND_BUF_SIZE];
  static int16_t  chunkBuf[MIC_CHUNK];
  uint32_t sendBufPos = 0;
  uint32_t lastSendMs = millis();

  while (true) {
    EventBits_t bits = xEventGroupGetBits(stateEvent);
    
    // ← QUAN TRỌNG: dừng mic khi WAITING hoặc PLAYING
    if (bits & (EV_WAITING | EV_PLAYING)) {
      sendBufPos = 0;          // xóa buffer cũ tránh gửi tiếng loa
      lastSendMs = millis();   // reset timer
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    size_t bytesRead = 0;
    i2s_read(I2S_PORT_RX, chunkBuf, sizeof(chunkBuf), &bytesRead, pdMS_TO_TICKS(10));

    if (bytesRead > 0) {
      size_t toCopy = min(bytesRead, (size_t)(SEND_BUF_SIZE - sendBufPos));
      memcpy(sendBuf + sendBufPos, chunkBuf, toCopy);
      sendBufPos += toCopy;
    }

    uint32_t now = millis();
    bool timeUp  = (now - lastSendMs) >= (RECORD_SECONDS * 1000UL);
    bool bufFull = (sendBufPos >= SEND_BUF_SIZE);

    if (wsConnected && (timeUp || bufFull)) {
      if (hasVoice((int16_t*)sendBuf, sendBufPos / 2)) {
        Serial.printf("[Mic] Sending %u bytes\n", sendBufPos);
        ws.sendBIN(sendBuf, sendBufPos);
        xEventGroupClearBits(stateEvent, EV_LISTENING);
        xEventGroupSetBits(stateEvent, EV_WAITING);
      } else {
        Serial.println("[Mic] Silence, skip");
      }
      sendBufPos = 0;
      lastSendMs = now;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ===== Task 2: WSTask (Core 0) =====
void wsTask(void* arg) {
  Serial.println("[WSTask] started");

  ws.begin(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(onWsEvent);
  ws.setReconnectInterval(3000);

  uint32_t waitStart = 0;

  while (true) {
    ws.loop();  // non-blocking, xử lý recv + ping

    // Timeout WAITING
    EventBits_t bits = xEventGroupGetBits(stateEvent);
    if (bits & EV_WAITING) {
      if (waitStart == 0) waitStart = millis();
      if (millis() - waitStart > 15000) {
        Serial.println("[WS] Timeout → LISTENING");
        xEventGroupClearBits(stateEvent, EV_WAITING);
        xEventGroupSetBits(stateEvent, EV_LISTENING);
        waitStart = 0;
      }
    } else {
      waitStart = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ===== PlayTask — fix amp + debug =====
void playTask(void* arg) {
  Serial.println("[PlayTask] started");

  while (true) {
    AudioChunk chunk;
    if (xQueueReceive(playQueue, &chunk, portMAX_DELAY) == pdTRUE) {
      
      if (chunk.isEnd) {
        Serial.println("[Play] END → wait DMA flush...");
        vTaskDelay(pdMS_TO_TICKS(300));
        digitalWrite(I2S_SD_SPK, LOW);   // tắt amp
        i2s_zero_dma_buffer(I2S_PORT_TX);
        
        // ← Thêm delay nhỏ sau khi loa phát xong
        // tránh mic thu ngay tiếng vang còn lại
        vTaskDelay(pdMS_TO_TICKS(500));
        
        xEventGroupClearBits(stateEvent, EV_PLAYING);
        xEventGroupSetBits(stateEvent, EV_LISTENING);
        Serial.println("[Play] → LISTENING");
        continue;
      }

      // Bật amp chunk đầu tiên
      digitalWrite(I2S_SD_SPK, HIGH);
      vTaskDelay(pdMS_TO_TICKS(10));

      size_t written = 0;
      esp_err_t err = i2s_write(I2S_PORT_TX, chunk.data, chunk.len, &written, portMAX_DELAY);
      Serial.printf("[Play] %u/%u bytes (err=%d)\n", written, chunk.len, err);
      free(chunk.data);
    }
  }
}

// ===== I2S Setup =====
void setupI2S() {
  i2s_config_t rx_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = MIC_CHUNK,
    .use_apll = false
  };
  i2s_pin_config_t rx_pins = {
    .bck_io_num = I2S_SCK, .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_SD
  };
  i2s_driver_install(I2S_PORT_RX, &rx_cfg, 0, NULL);
  i2s_set_pin(I2S_PORT_RX, &rx_pins);

  i2s_config_t tx_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = PLAY_CHUNK_SIZE / 2,
    .use_apll = false,
    .tx_desc_auto_clear = true   // tránh noise khi buffer rỗng
  };
  i2s_pin_config_t tx_pins = {
    .bck_io_num = I2S_BCLK_SPK, .ws_io_num = I2S_LRC_SPK,
    .data_out_num = I2S_DOUT_SPK, .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_PORT_TX, &tx_cfg, 0, NULL);
  i2s_set_pin(I2S_PORT_TX, &tx_pins);
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  pinMode(I2S_SD_SPK, OUTPUT);
  digitalWrite(I2S_SD_SPK, HIGH);

  setupI2S();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nWiFi: %s\n", WiFi.localIP().toString().c_str());

  // Tạo RTOS objects
  playQueue  = xQueueCreate(16, sizeof(AudioChunk));  // 16 chunk buffer
  stateEvent = xEventGroupCreate();
  xEventGroupSetBits(stateEvent, EV_LISTENING);

  // Tạo tasks — pin vào core cụ thể
  xTaskCreatePinnedToCore(micTask, "MicTask", 8192,  NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(wsTask,  "WSTask",  8192,  NULL, 4, NULL, 0);
  xTaskCreatePinnedToCore(playTask,"PlayTask",4096,  NULL, 5, NULL, 1);
}

void loop() {
  vTaskDelay(portMAX_DELAY); // loop() không làm gì, tasks lo hết
}