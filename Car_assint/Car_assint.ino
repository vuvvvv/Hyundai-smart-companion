
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SDA_PIN 21
#define SCL_PIN 22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(
  SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET
);

RTC_PCF8563 rtc;

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found!");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("PCF8563 RTC Test");
    display.display();
  }

  if (!rtc.begin()) {
    Serial.println("PCF8563 not found! Check wiring.");
    while (true) delay(100);
  }
 
  rtc.adjust(DateTime(2026, 10, 26, 18, 55, 0));
  

  // لا نستخدم rtc.adjust() هنا حتى لا نعيد ضبط
  // الوقت في كل مرة نعيد فيها تشغيل ESP32.
  if (rtc.lostPower()) {
    Serial.println("Clock lost power or time is invalid.");
    Serial.println("Set the time once using rtc.adjust().");
  } else {
    Serial.println("RTC detected. Reading stored time.");
  }
}

void loop() {
  DateTime now = rtc.now();

  char dateText[20];
  char timeText[12];

  snprintf(dateText, sizeof(dateText), "%04d-%02d-%02d",
           now.year(), now.month(), now.day());

  snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d",
           now.hour(), now.minute(), now.second());

  Serial.print(dateText);
  Serial.print("  ");
  Serial.println(timeText);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("PCF8563 RTC");

  display.setTextSize(2);
  display.setCursor(8, 22);
  display.println(timeText);

  display.setTextSize(1);
  display.setCursor(15, 48);
  display.println(dateText);

  display.display();

  delay(1000);
}
