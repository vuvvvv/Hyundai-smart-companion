// ===== alerts.h — قواعد التنبيهات + جلسة القيادة والاستراحة =====
// كل تنبيه: نغمة مستواه فقط (لا يُنطق) + الشاشة تعرض المصدر والقيمة.
// لا تنبيه من قيمة غير صالحة أو قديمة. كل الحدود والمدد في config.h.
#pragma once
#include <Arduino.h>

namespace alerts {

enum class Id : uint8_t { None, Coolant, Speed, Vibration, CabinHot, CabinCold, Rest };

// ما تعرضه الشاشة الآن
struct View {
  Id       id = Id::None;
  uint8_t  level = 0;        // 1 حرج، 2 تحذير، 3 تذكير
  bool     critical = false; // يبقى ما دامت الحالة مستمرة
  uint32_t sinceMs = 0;
  char     title[22] = "";   // مصدر المشكلة
  char     value[10] = "";   // القيمة الحالية
  char     unit[8]   = "";
  uint8_t  intensity = 0;    // الاهتزاز: 1..5 بدل رقم الانتقالات الخام
};

void begin();
void loop();

// عند التشغيل: التنبيهات (غير الحرجة) تنتظر حتى ينتهي الترحيب، فلا يتسابقان على الصوت والشاشة.
// تُستدعى من الحلقة الرئيسية بعد أن يُطلب الترحيب أو يُتجاوز.
void releaseStartup();

bool current(View& v);       // false = لا تنبيه ظاهر
bool restPending();          // ذُكّر بالاستراحة ولم يسترح بعد: رمز الكوب في شريط الحالة

void test(Id id);            // ALERTTEST: عرض التنبيه ونغمته بلا شرط (للتجربة)
void fakeSession(uint16_t minutes);   // SESSION n: تقديم الجلسة لتجربة الاستراحة
void printStatus(Print& out = Serial);

}  // namespace alerts
