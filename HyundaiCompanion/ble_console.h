// ===== ble_console.h — أوامر Serial من الجوال عبر BLE =====
// خدمة UART قياسية (Nordic UART): تعمل مع تطبيقات مثل Serial Bluetooth Terminal.
// الكتابة تحتاج اقتراناً مشفراً بالرمز (BLE_PASSKEY)، والأمر يُنفذ في الحلقة الرئيسية لا في مهمة البلوتوث.
#pragma once
#include <Arduino.h>

namespace ble_console {

void begin();                          // بعد obd::begin (البلوتوث مُهيأ)
void loop();                           // يرسل أجزاء الرد المتراكمة
bool takeLine(char* out, size_t n);    // أمر وصل من الجوال؟
bool connected();
Print& out();                          // مخرج الرد: يطبع في Serial والجوال معاً

}  // namespace ble_console
