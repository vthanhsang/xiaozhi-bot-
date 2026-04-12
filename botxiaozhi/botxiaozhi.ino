#include <TFT_eSPI.h>
#include "RoboEyesTFT_eSPI.h"

TFT_eSPI tft = TFT_eSPI();
TFT_RoboEyes roboEyes(tft, false, 3);

// ===== TIMER =====
unsigned long lastChange = 0;
int moodIndex = 0;

void setup() {
  Serial.begin(115200);

  // ===== bật đèn nền =====
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);

  // ===== TFT =====
  tft.init();
  tft.setRotation(0);

  // ===== RoboEyes =====
  roboEyes.begin(100); // ⚠️ 30 FPS cho ổn định (đừng 100)
  roboEyes.setColors(TFT_WHITE, TFT_BLACK);

  roboEyes.setAutoblinker(true, 2, 1);
  roboEyes.setIdleMode(true, 4, 0);

  // ⚠️ rất quan trọng (fit màn 240x240)
  roboEyes.setWidth(45, 45);
  roboEyes.setHeight(45, 45);
  roboEyes.setBorderradius(10, 10);
  roboEyes.setSpacebetween(30);

  lastChange = millis();
}

// ===== đổi mood =====
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

void loop() {
  // ===== update animation =====
  roboEyes.update();

  // ===== đổi mood mỗi 5s =====
  if (millis() - lastChange > 5000) {
    moodIndex++;
    if (moodIndex > 3) moodIndex = 0;

    changeMood(moodIndex);
    lastChange = millis();
  }
}