// ===== audio.h — DFPlayer + طابور الأولويات + جدول الأصوات =====
// مقطع واحد في كل لحظة. الأعلى أولوية يقطع الأدنى، والمتساوي ينتظر (4 عناصر).
// لا انتظار في أي دالة: الأوامر تُرسل والردود تُقرأ في loop().
#pragma once
#include <Arduino.h>
#include "data_model.h"

namespace audio {

// ================== مستويات الأولوية ==================
enum class Prio : uint8_t {
  Critical = 1,   // نغمة تنبيه عالي
  Warning  = 2,   // نغمة تنبيه متوسط
  Reminder = 3,   // نغمة تنبيه بسيط
  Info     = 4,   // مقاطع منطوقة: ترحيب، وقت، تاريخ
};

// ================== جدول الأصوات ==================
// رقم الملف على البطاقة: /mp3/0001.mp3 ...  (الجدول الكامل: SD_card/جدول_الأصوات.csv)
namespace snd {
constexpr uint16_t GOOD_MORNING = 1;
constexpr uint16_t GOOD_EVENING = 2;
constexpr uint16_t YA           = 3;     // "يا"
constexpr uint16_t OWNER_NAME   = 4;
constexpr uint16_t AM           = 5;     // صباحاً
constexpr uint16_t PM           = 6;     // مساءً

constexpr uint16_t TONE_LOW     = 10;    // المستوى 3
constexpr uint16_t TONE_MID     = 11;    // المستوى 2
constexpr uint16_t TONE_HIGH    = 12;    // المستوى 1

constexpr uint16_t W_TIME_NOW   = 50;    // الساعة الآن
constexpr uint16_t W_TODAY_IS   = 51;    // اليوم هو
constexpr uint16_t W_DATE_IS    = 52;    // تاريخ اليوم هو
constexpr uint16_t W_FROM       = 53;    // من
constexpr uint16_t W_AND        = 54;    // و

constexpr uint16_t weekday(uint8_t dow)  { return 40 + dow; }        // 0 = الأحد
constexpr uint16_t month(uint8_t m)      { return 60 + m; }          // 1..12
constexpr uint16_t hour12(uint8_t h)     { return 80 + h; }          // 1..12 "الساعة الثالثة"
constexpr uint16_t minute(uint8_t m)     { return 100 + m; }         // 1..59 "خمس دقائق"
constexpr uint16_t year(uint16_t y)      { return 200 + (y - 2000); }
constexpr uint16_t dayOfMonth(uint8_t d) { return 300 + d; }         // 1..31 "الأول"

constexpr uint16_t YEAR_FIRST = 2026;    // السنوات المسجلة فقط
constexpr uint16_t YEAR_LAST  = 2028;

// ---------- جمل مدمجة مسبقاً (tools/build_phrases.ps1) ----------
// كل انتقال بين ملفين على DFPlayer فيه توقف، فالأجزاء الثابتة مدمجة.
// عدد الملفات محدود (~368): DFPlayer يفهرس البطاقة عند الإقلاع، و1845 ملفاً منعته من العمل.
constexpr uint16_t greeting(bool morning)        { return morning ? 401 : 402; }  // صباح/مساء الخير يا عبد العزيز
constexpr uint16_t andMinute(uint8_t m, bool pm) { return (pm ? 700 : 600) + m; } // و + خمس دقائق + صباحاً/مساءً
constexpr uint16_t hourOnly(uint8_t h12, bool pm){ return (pm ? 980 : 880) + h12; } // الساعة الثالثة + صباحاً/مساءً
constexpr uint16_t todayIs(uint8_t dow)          { return 440 + dow; }            // اليوم هو + الجمعة
constexpr uint16_t dateIs(uint8_t day)           { return 500 + day; }            // تاريخ اليوم هو + الحادي والعشرون
// "من أكتوبر 2026"، أو "من أكتوبر" لسنة غير مسجلة
constexpr uint16_t monthYear(uint8_t m, uint16_t y) {
  return (y >= YEAR_FIRST && y <= YEAR_LAST) ? 800 + (y - YEAR_FIRST) * 12 + m : 560 + m;
}
}  // namespace snd

void begin();
void loop();

// تشغيل: ترجع false إن رُفض الطلب (الطابور ممتلئ بأولويات أعلى)
bool play(Prio p, const uint16_t* clips, uint8_t count);
bool tone(Prio p);                      // نغمة المستوى فقط (التنبيهات لا تُنطق)
bool sayGreeting(const ClockTime& c);   // صباح/مساء الخير يا عبد العزيز
bool sayTime(const ClockTime& c);
bool sayDate(const ClockTime& c);

void stopAll();
void testAll();                         // تشغيل كل ملفات الجدول بالترتيب
void setVolume(uint8_t v);              // 0..30
bool ready();                           // القطعة ترد والبطاقة موجودة
bool idle();
void printStatus(Print& out = Serial);

}  // namespace audio
