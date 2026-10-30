// ===== sensors.h — الساعة PCF8563 + AHT20 + BMP280 + اهتزاز HW-072 =====
// كل I2C يُلمس من الحلقة الرئيسية فقط (مهمة OBD لا تلمس الناقل)، لذلك لا يحتاج قفلاً.
#pragma once
#include <Arduino.h>
#include <RTClib.h>

namespace sensors {

void begin();
void loop();                         // يُستدعى كل دورة، والجدولة داخلية بـ millis()

bool i2cProbe(uint8_t addr);         // هل القطعة ترد على عنوانها؟
bool setTime(const DateTime& t);     // أمر SET
void setVibMonitor(bool on);         // طباعة عدد انتقالات الاهتزاز كل نافذة (للمعايرة)
bool vibMonitor();
void printStatus(Print& out = Serial);

}  // namespace sensors
