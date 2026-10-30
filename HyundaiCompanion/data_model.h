// ===== data_model.h — لقطة البيانات المشتركة =====
// مساران يكتبان هنا: المسار المحلي (loop) ومهمة OBD. الشاشة والتنبيهات والصوت تقرأ فقط.
// كل وصول عبر Mutex قصير: نسخ أو تعديل حقول فقط، بلا عمليات طويلة داخله.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

// ================== قاعدة القيمة الصالحة ==================
// سبب عدم صلاحية القيمة (يُعرض رمزه بجانب --)
enum class ReadErr : uint8_t {
  None = 0,
  NoData,        // لم تصل قراءة بعد
  OutOfRange,    // قيمة خارج المدى المنطقي
  Stuck,         // قيمة ثابتة لفترة طويلة
  DeviceFail,    // القطعة لا ترد
  Unsupported,   // السيارة لا تدعم هذا الـ PID
};

// كل قيمة: الرقم + هل هي صالحة + وقت آخر تحديث
struct Reading {
  float    value     = 0.0f;
  bool     valid     = false;
  uint32_t updatedMs = 0;
  ReadErr  err       = ReadErr::NoData;

  // قيمة جديدة: تُقبل فقط داخل المدى
  void set(float v, float minV, float maxV, uint32_t nowMs = millis()) {
    if (isnan(v) || v < minV || v > maxV) { fail(ReadErr::OutOfRange); return; }
    value = v; valid = true; updatedMs = nowMs; err = ReadErr::None;
  }

  // القيمة القديمة تبقى للتشخيص، لكنها لم تعد صالحة
  void fail(ReadErr e) { valid = false; err = e; }

  // صالحة وحديثة؟ لا عرض ولا تنبيه من غير ذلك
  bool fresh(uint32_t maxAgeMs, uint32_t nowMs = millis()) const {
    return valid && (nowMs - updatedMs) <= maxAgeMs;
  }
};

// ================== الوقت ==================
struct ClockTime {
  uint16_t year = 0;
  uint8_t  month = 0, day = 0, hour = 0, minute = 0, second = 0;
  uint8_t  dow = 0;          // 0 = الأحد (مثل RTClib)
  bool     valid = false;    // false = يُعرض --:-- ولا يُنطق الوقت
  bool     fromBackup = false;  // من millis() لأن قراءة الساعة فشلت مؤقتاً
};

// ================== حالة OBD ==================
enum class ObdLink : uint8_t {
  Disconnected,   // غير متصل
  Connecting,     // جاري الاتصال
  Initializing,   // أوامر AT
  Active,         // يقرأ PIDs
  CarAsleep,      // البلوتوث متصل لكن السيارة مطفأة
  Backoff,        // انتظار متزايد قبل المحاولة التالية
};

enum class Support : uint8_t { Unknown, Yes, No };

// ================== صحة القطع (شريط الحالة) ==================
enum HealthFlag : uint16_t {
  HF_OLED_FAIL      = 1 << 0,
  HF_RTC_FAIL       = 1 << 1,
  HF_CLOCK_UNSET    = 1 << 2,   // الساعة فقدت الوقت
  HF_AHT_FAIL       = 1 << 3,
  HF_BMP_FAIL       = 1 << 4,
  HF_AUDIO_FAIL     = 1 << 5,
  HF_SD_MISSING     = 1 << 6,
  HF_I2C_RECOVERED  = 1 << 7,
  HF_ABNORMAL_RESET = 1 << 8,   // آخر إعادة تشغيل غير طبيعية
  HF_LOW_MEMORY     = 1 << 9,
};

// ================== اللقطة ==================
struct Snapshot {
  // المسار المحلي
  ClockTime clock;
  Reading   cabinTemp;     // AHT20 «داخل»
  Reading   humidity;      // AHT20
  Reading   pressure;      // BMP280 (hPa)
  uint16_t  vibCount = 0;  // انتقالات آخر نافذة

  // مسار OBD
  ObdLink   obdLink = ObdLink::Disconnected;
  Reading   speed;         // km/h
  Reading   rpm;
  Reading   coolant;       // °C
  Reading   ambient;       // °C «خارج»
  Reading   battery;       // V من ATRV
  Support   ambientSupport = Support::Unknown;
  uint32_t  obdErrors = 0;

  uint16_t  health = 0;    // HealthFlag
};

// ================== الوصول المحمي ==================
namespace model {

inline Snapshot          g_snap;
inline SemaphoreHandle_t g_mutex = nullptr;
constexpr TickType_t     LOCK_WAIT = pdMS_TO_TICKS(20);

inline bool begin() {
  if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
  return g_mutex != nullptr;
}

// تعديل اللقطة:  model::update([](Snapshot& s){ s.speed.set(...); });
// يرجع false إن لم يحصل على القفل خلال 20ms (التعديل يُتجاهل ويُحاول في الدورة التالية)
template <typename Fn>
inline bool update(Fn&& fn) {
  if (!g_mutex || xSemaphoreTake(g_mutex, LOCK_WAIT) != pdTRUE) return false;
  fn(g_snap);
  xSemaphoreGive(g_mutex);
  return true;
}

// نسخة كاملة للقراءة؛ القارئ يعمل على نسخته بلا قفل
inline bool read(Snapshot& out) {
  if (!g_mutex || xSemaphoreTake(g_mutex, LOCK_WAIT) != pdTRUE) return false;
  out = g_snap;
  xSemaphoreGive(g_mutex);
  return true;
}

inline void setHealth(uint16_t flag, bool on) {
  update([=](Snapshot& s) { if (on) s.health |= flag; else s.health &= ~flag; });
}

}  // namespace model
