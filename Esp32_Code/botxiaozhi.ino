#include <driver/i2s.h>
#include <WiFi.h>
#include <WebSocketsClient.h>

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

#define SAMPLE_RATE     16000
#define CHUNK_SIZE      512
#define RECORD_SECONDS  3
#define SEND_BUF_SIZE   (SAMPLE_RATE * 2 * RECORD_SECONDS)

// ===== State Machine =====
enum State { STATE_LISTENING, STATE_WAITING, STATE_PLAYING };
State currentState = STATE_LISTENING;

int16_t chunkBuf[CHUNK_SIZE];
uint8_t sendBuf[SEND_BUF_SIZE];
uint32_t sendBufPos = 0;
uint32_t lastSendMs = 0;
uint32_t waitingStartMs = 0;  

WebSocketsClient ws;
bool wsConnected = false;

// ===== Phát audio — BLOCKING cho đến khi hết =====
void playAudio(uint8_t* data, size_t len) {
  currentState = STATE_PLAYING;
  Serial.printf("[PLAY] Playing %u bytes\n", len);

  size_t written = 0;
  const size_t PLAY_CHUNK = 1024;
  for (size_t i = 0; i < len; i += PLAY_CHUNK) {
    size_t toWrite = min(PLAY_CHUNK, len - i);
    i2s_write(I2S_PORT_TX, data + i, toWrite, &written, portMAX_DELAY);
    ws.loop(); // giữ WebSocket sống trong khi phát
  }

  // Xả DMA buffer, đợi loa phát hết
  delay(200);

  Serial.println("[PLAY] Done → back to LISTENING");
  sendBufPos = 0;     // xóa buffer mic (tránh thu tiếng loa cũ)
  lastSendMs = millis();
  currentState = STATE_LISTENING;
}

void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[WS] Connected");
      wsConnected = true;
      break;

    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected");
      wsConnected = false;
      // Nếu đang chờ mà mất kết nối → về LISTENING
      if (currentState == STATE_WAITING) {
        currentState = STATE_LISTENING;
      }
      break;

    case WStype_BIN:
      // Nếu server gửi gói rỗng → kết thúc audio
      if (length == 0) {
        Serial.println("[WS] Audio END");

        i2s_zero_dma_buffer(I2S_PORT_TX); // xóa buffer loa
        currentState = STATE_LISTENING;
        break;
      }

      // Nhận từng chunk → phát luôn
      if (currentState == STATE_WAITING || currentState == STATE_PLAYING) {
        size_t written;
        i2s_write(I2S_PORT_TX, payload, length, &written, portMAX_DELAY);

        currentState = STATE_PLAYING;
        Serial.printf("[WS] Playing chunk: %u bytes\n", length);
      }
      break;

    case WStype_TEXT:
      Serial.printf("[WS] Text: %s\n", (char*)payload);
      // Nếu server báo lỗi / không có gì → về LISTENING
      if (currentState == STATE_WAITING) {
        currentState = STATE_LISTENING;
      }
      break;

    default:
      break;
  }
}

bool hasVoice(int16_t* buf, size_t samples, int16_t threshold = 2000) {
  for (size_t i = 0; i < samples; i++) {
    if (abs(buf[i]) > threshold) return true;
  }
  return false;
}

void setupI2S() {
  i2s_config_t rx_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = CHUNK_SIZE,
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
    .dma_buf_len = CHUNK_SIZE,
    .use_apll = false
  };
  i2s_pin_config_t tx_pins = {
    .bck_io_num = I2S_BCLK_SPK, .ws_io_num = I2S_LRC_SPK,
    .data_out_num = I2S_DOUT_SPK, .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_PORT_TX, &tx_cfg, 0, NULL);
  i2s_set_pin(I2S_PORT_TX, &tx_pins);
}

void setup() {
  Serial.begin(115200);
  pinMode(I2S_SD_SPK, OUTPUT);
  digitalWrite(I2S_SD_SPK, HIGH);
  setupI2S();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nWiFi OK: %s\n", WiFi.localIP().toString().c_str());

  ws.begin(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(onWsEvent);
  ws.setReconnectInterval(3000);
}

void loop() {
  ws.loop();

  // Timeout WAITING
  if (currentState == STATE_WAITING && (millis() - waitingStartMs > 30000)) {
    Serial.println("[TIMEOUT] No reply in 30s → LISTENING");
    currentState = STATE_LISTENING;
    sendBufPos = 0;
    lastSendMs = millis();
  }

  if (currentState != STATE_LISTENING) return;

  size_t bytesRead = 0;
  i2s_read(I2S_PORT_RX, chunkBuf, sizeof(chunkBuf), &bytesRead, 10);

  // Luôn tích lũy vào buffer, không lọc VAD ở đây
  if (bytesRead > 0) {
    size_t toCopy = min(bytesRead, (size_t)(SEND_BUF_SIZE - sendBufPos));
    memcpy(sendBuf + sendBufPos, chunkBuf, toCopy);
    sendBufPos += toCopy;
  }

  uint32_t now = millis();
  bool timeUp  = (now - lastSendMs) >= (RECORD_SECONDS * 1000);
  bool bufFull = sendBufPos >= SEND_BUF_SIZE;

  if (wsConnected && (timeUp || bufFull)) {
    // Chỉ gửi nếu buffer có tiếng thực sự
    if (hasVoice((int16_t*)sendBuf, sendBufPos / 2, 500)) {
      Serial.printf("[MIC] Sending %u bytes → WAITING\n", sendBufPos);
      ws.sendBIN(sendBuf, sendBufPos);
      currentState = STATE_WAITING;
      waitingStartMs = millis();  // ← fix bug 2
    } else {
      Serial.println("[MIC] Silence, skip");
    }
    sendBufPos = 0;
    lastSendMs = now;
  }
}