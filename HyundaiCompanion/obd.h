// ===== obd.h — مسار OBD: مهمة FreeRTOS مستقلة، آلة حالات، أوامر ELM327، فك الردود =====
// المهمة هي الوحيدة التي تلمس SerialBT، وتكتب قيمها في لقطة البيانات.
// الحلقة الرئيسية لا تنتظرها أبداً: انقطاع OBD لا يوقف الجهاز.
// كل الأوامر قراءة فقط: لا يُرسل أي أمر يغيّر شيئاً في السيارة.
#pragma once
#include <Arduino.h>

namespace obd {

void begin();                      // تهيئة البلوتوث وبدء المهمة

void printStatus(Print& out = Serial);
void setRawMonitor(bool on);       // طباعة كل أمر ورده (للاختبار)
bool rawMonitor();
bool sendRaw(const char* cmd);     // أمر يدوي للقراءة (AT.., 01.., 09..) يُنفذ داخل المهمة
void setCmdGap(uint16_t ms);       // الفاصل بين الأوامر
uint16_t cmdGap();
uint32_t stackFree();              // أقل مساحة مكدس بقيت للمهمة (بايت) — للتشخيص

}  // namespace obd
