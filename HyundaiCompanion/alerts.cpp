// ===== alerts.cpp =====
// الشرط الأصلي لا يتغير؛ حد الرجوع يمنع التذبذب قرب الحد فقط.
// يعمل في الحلقة الرئيسية مثل الشاشة، فلا يحتاج قفلاً للتنبيه المعروض.
#include "alerts.h"
#include "config.h"
#include "data_model.h"
#include "audio.h"

namespace alerts {
namespace {

const char* const ID_NAMES[] = {"none", "coolant", "speed", "vibration", "cabin hot", "cabin cold", "rest"};

// ================== التنبيه المعروض ==================
View shown;
bool hasShown = false;

bool shownExpired(uint32_t now) {
  return !shown.critical && now - shown.sinceMs >= cfg::ALERT_SHOW_MS;
}

// النغمة دائماً (طابور الصوت يرتبها بالأولوية)، والشاشة لا يحل فيها الأقل أهمية محل الأهم
void raise(Id id, uint8_t level, bool critical, const char* title, const char* value, const char* unit,
           uint8_t intensity = 0) {
  uint32_t now = millis();
  audio::tone((audio::Prio)level);
  LOG_I("ALERT [%u] %s %s%s", level, title, value, unit);

  if (hasShown && !shownExpired(now) && shown.id != id && shown.level < level) return;
  shown.id = id;
  shown.level = level;
  shown.critical = critical;
  shown.sinceMs = now;
  strlcpy(shown.title, title, sizeof(shown.title));
  strlcpy(shown.value, value, sizeof(shown.value));
  strlcpy(shown.unit, unit, sizeof(shown.unit));
  shown.intensity = intensity;
  hasShown = true;
}

// شدة الاهتزاز: الحد = 1، وكل ضعف إضافي درجة، حتى 5
uint8_t vibIntensity(uint16_t count) {
  uint16_t lvl = count / cfg::VIB_STRONG_COUNT;
  return lvl < 1 ? 1 : lvl > 5 ? 5 : lvl;
}

void clearShown(Id id) {
  if (hasShown && shown.id == id) hasShown = false;
}

void fmt(char* buf, size_t n, float v, int decimals) {
  snprintf(buf, n, decimals ? "%.1f" : "%.0f", v);
}

// ================== حرارة ماء المحرك (حرج) ==================
// 3 قراءات متتالية ≥105 ← تنبيه يبقى على الشاشة ويتكرر صوته كل 5 دقائق؛ يزول تحت 100
uint32_t coolSample = 0;
uint8_t  coolAbove = 0;
bool     coolActive = false;
uint32_t coolSoundMs = 0;

void checkCoolant(const Snapshot& s, uint32_t now) {
  if (!s.coolant.fresh(cfg::OBD_STALE_MS, now)) {
    coolAbove = 0;
    if (coolActive) { coolActive = false; clearShown(Id::Coolant); }
    return;
  }
  if (s.coolant.updatedMs == coolSample) return;   // ننتظر قراءة جديدة
  coolSample = s.coolant.updatedMs;
  float v = s.coolant.value;
  char val[10];
  fmt(val, sizeof(val), v, 0);

  if (!coolActive) {
    if (v >= cfg::COOLANT_ALERT_C) {
      if (++coolAbove >= cfg::COOLANT_CONFIRM) {
        coolActive = true;
        coolAbove = 0;
        coolSoundMs = now;
        raise(Id::Coolant, 1, true, "ENGINE: COOLANT", val, "\xF8" "C");
      }
    } else {
      coolAbove = 0;
    }
    return;
  }

  if (v < cfg::COOLANT_REARM_C) {
    coolActive = false;
    clearShown(Id::Coolant);
    LOG_I("ALERT coolant cleared");
    return;
  }
  if (hasShown && shown.id == Id::Coolant) strlcpy(shown.value, val, sizeof(shown.value));  // القيمة حية
  if (now - coolSoundMs >= cfg::COOLANT_REPEAT_MS) {
    coolSoundMs = now;
    raise(Id::Coolant, 1, true, "ENGINE: COOLANT", val, "\xF8" "C");
  }
}

// ================== السرعة (تحذير) ==================
// قراءتان متتاليتان ≥80 ← مرة لكل تجاوز، و60 ث على الأقل بين تنبيهين؛ يُعاد تحت 75
uint32_t speedSample = 0;
uint8_t  speedAbove = 0;
bool     speedActive = false;
uint32_t speedLastMs = 0;
bool     speedEver = false;

void checkSpeed(const Snapshot& s, uint32_t now) {
  if (!s.speed.fresh(cfg::OBD_STALE_MS, now)) { speedAbove = 0; return; }
  if (s.speed.updatedMs == speedSample) return;
  speedSample = s.speed.updatedMs;
  float v = s.speed.value;

  if (speedActive) {
    if (v < cfg::SPEED_REARM_KMH) speedActive = false;
    return;
  }
  if (v < cfg::SPEED_ALERT_KMH) { speedAbove = 0; return; }
  if (++speedAbove < cfg::SPEED_CONFIRM) return;

  speedActive = true;
  speedAbove = 0;
  if (speedEver && now - speedLastMs < cfg::SPEED_MIN_GAP_MS) return;
  speedEver = true;
  speedLastMs = now;
  char val[10];
  fmt(val, sizeof(val), v, 0);
  raise(Id::Speed, 2, false, "SPEED", val, "km/h");
}

// ================== الاهتزاز القوي (تحذير، تجريبي) ==================
uint32_t vibLastMs = 0;
bool     vibEver = false;

// السرعة غير معروفة (OBD غير متصل) = قد تكون السيارة تمشي: لا تنبيه، حتى لا يطلقه الطريق
void checkVibration(const Snapshot& s, uint32_t now) {
  if (!s.speed.fresh(cfg::OBD_STALE_MS, now)) return;
  bool moving = s.speed.value > cfg::VIB_MOVING_KMH;
  uint16_t thr = moving ? cfg::VIB_STRONG_COUNT_MOVING : cfg::VIB_STRONG_COUNT;
  if (s.vibCount < thr) return;
  if (vibEver && now - vibLastMs < cfg::VIB_ALERT_GAP_MS) return;
  vibEver = true;
  vibLastMs = now;
  char val[10];
  snprintf(val, sizeof(val), "%u", s.vibCount);
  raise(Id::Vibration, 2, false, "STRONG VIBRATION", val, "", vibIntensity(s.vibCount));
}

// ================== حرارة المقصورة (تحذير، مرة لكل دورة تشغيل) ==================
bool hotFired = false, coldFired = false;

void checkCabin(const Snapshot& s, uint32_t now) {
  if (!s.cabinTemp.fresh(cfg::ENV_STALE_MS, now)) return;
  float v = s.cabinTemp.value;
  char val[10];
  fmt(val, sizeof(val), v, 1);
  if (!hotFired && v > cfg::CABIN_HOT_C) {
    hotFired = true;
    raise(Id::CabinHot, 2, false, "CABIN HOT", val, "\xF8" "C");
  }
  if (!coldFired && v < cfg::CABIN_COLD_C) {
    coldFired = true;
    raise(Id::CabinCold, 2, false, "CABIN COLD", val, "\xF8" "C");
  }
}

// دورة تشغيل جديدة (دوران المحرك، أو تشغيل الجهاز): تُعاد حالات "مرة لكل دورة"
void newIgnitionCycle() {
  hotFired = coldFired = false;
  LOG_I("ignition cycle start");
}

// ================== جلسة القيادة ==================
// مع OBD: تبدأ بـ RPM > 0 لمدة 10 ث، وتنتهي بإطفاء المحرك 10 دقائق فأكثر.
// بلا OBD: مدة تشغيل الجهاز (يعمل مع السويتش). انقطاع OBD أثناء الجلسة لا يصفّر العداد.
enum class Engine : uint8_t { Unknown, Off, Running };

bool     sessionActive = false;
bool     sessionFromEngine = false;
uint32_t sessionStartMs = 0;
uint32_t runningSince = 0, offSince = 0;
bool     engineWasOff = false;
bool     obdSeen = false;
bool     restReminded = false;
uint32_t nextRestMs = 0;

Engine engineState(const Snapshot& s, uint32_t now) {
  if (s.obdLink == ObdLink::CarAsleep) return Engine::Off;
  if (s.rpm.fresh(cfg::OBD_STALE_MS, now)) return s.rpm.value > 0 ? Engine::Running : Engine::Off;
  return Engine::Unknown;
}

void startSession(bool fromEngine, uint32_t now) {
  sessionActive = true;
  sessionFromEngine = fromEngine;
  sessionStartMs = now;
  restReminded = false;
  nextRestMs = cfg::REST_REMINDER_MS;
  LOG_I("session start (%s)", fromEngine ? "engine" : "device");
}

void endSession() {
  sessionActive = false;
  restReminded = false;
  if (hasShown && shown.id == Id::Rest) hasShown = false;
  LOG_I("session end (rest)");
}

void updateSession(const Snapshot& s, uint32_t now) {
  if (s.obdLink == ObdLink::Active || s.obdLink == ObdLink::CarAsleep) obdSeen = true;
  Engine e = engineState(s, now);

  if (e == Engine::Running) {
    offSince = 0;
    if (!runningSince) runningSince = now;
    if (now - runningSince >= cfg::SESSION_START_RPM_MS) {
      if (engineWasOff) { engineWasOff = false; newIgnitionCycle(); }
      if (!sessionActive) startSession(true, now);
    }
  } else if (e == Engine::Off) {
    runningSince = 0;
    engineWasOff = true;
    if (!offSince) offSince = now;
    if (sessionActive && now - offSince >= cfg::SESSION_BREAK_MS) endSession();
  } else {
    runningSince = 0;     // OBD منقطع: العداد يستمر ولا يُصفّر
  }

  // بلا OBD منذ التشغيل: الجلسة = مدة تشغيل الجهاز
  if (!sessionActive && !obdSeen) startSession(false, now);

  // الاستراحة: رمز كوب قهوة + تنبيه بسيط عند 30 دقيقة
  if (sessionActive && now - sessionStartMs >= nextRestMs && (!restReminded || !cfg::REST_REMIND_ONCE)) {
    restReminded = true;
    char val[10];
    snprintf(val, sizeof(val), "%lu", (unsigned long)((now - sessionStartMs) / 60000));
    raise(Id::Rest, 3, false, "TAKE A BREAK", val, "min");
    nextRestMs += cfg::REST_REMINDER_MS;
  }
}

uint32_t lastTickMs = 0;

// ================== بوابة بداية التشغيل ==================
// تُفتح بعد انتهاء الترحيب (أو إن كان الصوت معطلاً)، أو بعد مهلة قصوى. مرة واحدة فقط.
bool startupReleased = false;
bool startupOpen = false;

bool startupGate(uint32_t now) {
  if (startupOpen) return true;
  bool greetingDone = startupReleased && (audio::idle() || !audio::ready());
  if (greetingDone || now >= cfg::ALERT_STARTUP_MAX_MS) {
    startupOpen = true;
    LOG_I("alerts armed (%s)", greetingDone ? "after greeting" : "timeout");
  }
  return startupOpen;
}

}  // namespace

// ================== الواجهة العامة ==================
void begin() {
  newIgnitionCycle();     // تشغيل الجهاز = بداية دورة (حتى يتوفر RPM)
}

void loop() {
  uint32_t now = millis();
  if (now - lastTickMs < cfg::ALERT_TICK_MS) return;
  lastTickMs = now;

  Snapshot s;
  if (!model::read(s)) return;

  updateSession(s, now);
  checkCoolant(s, now);           // الحرج لا ينتظر: يقطع الترحيب فوراً
  if (!startupGate(now)) return;  // البقية بعد الترحيب
  checkSpeed(s, now);
  checkVibration(s, now);
  checkCabin(s, now);
}

void releaseStartup() { startupReleased = true; }

bool current(View& v) {
  if (!hasShown) return false;
  if (shownExpired(millis())) { hasShown = false; return false; }
  v = shown;
  return true;
}

bool restPending() { return sessionActive && restReminded; }

void test(Id id) {
  switch (id) {
    case Id::Coolant:   raise(id, 1, false, "ENGINE: COOLANT", "106", "\xF8" "C"); break;
    case Id::Speed:     raise(id, 2, false, "SPEED", "83", "km/h"); break;
    case Id::Vibration: raise(id, 2, false, "STRONG VIBRATION", "255", "", vibIntensity(255)); break;
    case Id::CabinHot:  raise(id, 2, false, "CABIN HOT", "41.2", "\xF8" "C"); break;
    case Id::CabinCold: raise(id, 2, false, "CABIN COLD", "13.5", "\xF8" "C"); break;
    case Id::Rest:      raise(id, 3, false, "TAKE A BREAK", "30", "min"); break;
    default: break;
  }
}

void fakeSession(uint16_t minutes) {
  uint32_t now = millis();
  if (!sessionActive) startSession(false, now);
  uint32_t back = (uint32_t)minutes * 60000UL;
  sessionStartMs = now > back ? now - back : 0;
  restReminded = false;
  nextRestMs = cfg::REST_REMINDER_MS;
}

void printStatus(Print& out) {
  uint32_t now = millis();
  out.println("----- Alerts / Session -----");
  if (sessionActive)
    out.printf("  Session  %lu min (%s)  rest reminded=%s\n",
                  (unsigned long)((now - sessionStartMs) / 60000), sessionFromEngine ? "engine" : "device",
                  restReminded ? "yes" : "no");
  else
    out.println("  Session  -");
  out.printf("  Active   coolant=%s speed=%s  cabin hot/cold fired=%s/%s\n",
                coolActive ? "YES" : "no", speedActive ? "yes" : "no",
                hotFired ? "yes" : "no", coldFired ? "yes" : "no");
  View v;
  if (current(v)) out.printf("  Showing  %s [%u] %s %s%s\n", ID_NAMES[(int)v.id], v.level, v.title, v.value, v.unit);
}

}  // namespace alerts
