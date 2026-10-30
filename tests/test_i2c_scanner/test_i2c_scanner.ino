// ===== المرحلة 1: فحص ناقل I2C والمكتبات =====
// يفحص كل العناوين، يتعرف على القطع المتوقعة، ثم يجرب كل مكتبة على قطعتها.
// يعيد الفحص كل 10 ثوانٍ (أو عند كتابة أي حرف في Serial Monitor)
// لتجربة فصل قطعة وإعادتها أثناء التشغيل.

#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

#define SDA_PIN 21
#define SCL_PIN 22

struct KnownDevice { uint8_t addr; const char* name; bool required; };

const KnownDevice KNOWN[] = {
  {0x3C, "OLED SSD1306",     true},
  {0x51, "PCF8563 RTC",      true},
  {0x38, "AHT20",            true},
  {0x76, "BMP280 (SDO=GND)", false},
  {0x77, "BMP280 (SDO=VCC)", false},
};

RTC_PCF8563 rtc;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;

const char* knownName(uint8_t addr) {
  for (const auto& d : KNOWN) if (d.addr == addr) return d.name;
  return "UNKNOWN";
}

// 9 نبضات على SCL لتحرير قطعة عالقة تمسك SDA منخفضاً
void i2cBusRecovery() {
  Wire.end();
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9 && digitalRead(SDA_PIN) == LOW; i++) {
    digitalWrite(SCL_PIN, LOW);  delayMicroseconds(5);
    digitalWrite(SCL_PIN, HIGH); delayMicroseconds(5);
  }
  Wire.begin(SDA_PIN, SCL_PIN);
}

bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void scanBus() {
  Serial.println("\n========== I2C SCAN ==========");

  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, INPUT_PULLUP);
  if (digitalRead(SDA_PIN) == LOW || digitalRead(SCL_PIN) == LOW) {
    Serial.println("[BUS] SDA/SCL stuck LOW -> recovery (9 clocks)");
    i2cBusRecovery();
  } else {
    Wire.begin(SDA_PIN, SCL_PIN);
  }

  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (probe(a)) {
      Serial.printf("  0x%02X  %s\n", a, knownName(a));
      found++;
    }
  }
  Serial.printf("Devices found: %d\n", found);

  Serial.println("\n----- Expected devices -----");
  bool bmpSeen = probe(0x76) || probe(0x77);
  for (const auto& d : KNOWN) {
    if (!d.required) continue;
    Serial.printf("  %-14s 0x%02X : %s\n", d.name, d.addr, probe(d.addr) ? "OK" : "MISSING");
  }
  Serial.printf("  %-14s      : %s\n", "BMP280", bmpSeen ? "OK" : "MISSING");
}

void testLibraries() {
  Serial.println("\n----- Library tests -----");

  // OLED
  if (oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("I2C Scanner");
    oled.println("See Serial Monitor");
    oled.display();
    Serial.println("  OLED     : OK");
  } else {
    Serial.println("  OLED     : FAIL");
  }

  // الساعة: نتأكد أن RTClib تدعم PCF8563 وتكشف فقدان الوقت
  if (rtc.begin(&Wire)) {
    DateTime t = rtc.now();
    Serial.printf("  PCF8563  : OK  %04d-%02d-%02d %02d:%02d:%02d  lostPower=%s  running=%s\n",
                  t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second(),
                  rtc.lostPower() ? "YES" : "no",
                  rtc.isrunning() ? "yes" : "NO");
  } else {
    Serial.println("  PCF8563  : FAIL");
  }

  // AHT20
  if (aht.begin(&Wire)) {
    sensors_event_t hum, temp;
    aht.getEvent(&hum, &temp);
    Serial.printf("  AHT20    : OK  %.1f C  %.1f %%\n", temp.temperature, hum.relative_humidity);
  } else {
    Serial.println("  AHT20    : FAIL");
  }

  // BMP280
  uint8_t bmpAddr = 0;
  if (bmp.begin(0x76)) bmpAddr = 0x76;
  else if (bmp.begin(0x77)) bmpAddr = 0x77;
  if (bmpAddr) {
    Serial.printf("  BMP280   : OK at 0x%02X  %.1f C  %.1f hPa\n",
                  bmpAddr, bmp.readTemperature(), bmp.readPressure() / 100.0F);
  } else {
    Serial.println("  BMP280   : FAIL (chip ID 0x58 expected; 0x60 = BME280)");
  }
}

void printSystemInfo() {
  Serial.println("\n----- System -----");
  Serial.printf("  Arduino-ESP32 core : %d.%d.%d\n",
                ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  Serial.printf("  Chip               : %s rev %d, %d cores\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("  Flash              : %u KB\n", ESP.getFlashChipSize() / 1024);
  Serial.printf("  Sketch / free app  : %u / %u KB\n",
                ESP.getSketchSize() / 1024, ESP.getFreeSketchSpace() / 1024);
  Serial.printf("  Free heap          : %u bytes\n", ESP.getFreeHeap());
  Serial.printf("  Reset reason       : %d\n", (int)esp_reset_reason());
}

void runAll() {
  scanBus();
  testLibraries();
  printSystemInfo();
  Serial.println("\nNext scan in 10 s (or send any key)");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  runAll();
}

void loop() {
  static uint32_t last = millis();
  bool keyPressed = false;
  while (Serial.available()) { Serial.read(); keyPressed = true; }

  if (keyPressed || millis() - last > 10000) {
    last = millis();
    runAll();
  }
}
