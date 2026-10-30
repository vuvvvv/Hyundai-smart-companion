// ===== sensors.cpp =====
#include "sensors.h"
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_BMP280.h>
#include "config.h"
#include "data_model.h"

namespace sensors {
namespace {

// ================== تتبع صحة كل قطعة ==================
// فشل ← مهلة متزايدة 1 ث ثم 5 ثم 30 كحد أقصى قبل إعادة التهيئة
struct Device {
  const char* name;
  uint16_t    flag;
  bool        ok = false;
  uint8_t     backoff = 0;
  uint32_t    nextTry = 0;
  uint32_t    errors = 0;

  Device(const char* n, uint16_t f) : name(n), flag(f) {}

  void markOk() {
    if (!ok) LOG_I("%s OK", name);
    ok = true;
    backoff = 0;
    model::setHealth(flag, false);
  }
  void markFail(uint32_t now) {
    errors++;
    if (ok) LOG_W("%s failed", name);
    ok = false;
    model::setHealth(flag, true);
    nextTry = now + cfg::RETRY_BACKOFF_MS[backoff];
    if (backoff < 2) backoff++;
  }
  bool canRetry(uint32_t now) const { return (int32_t)(now - nextTry) >= 0; }
  void retryNow() { ok = false; nextTry = millis(); backoff = 0; }
};

Device devRtc{"RTC", HF_RTC_FAIL};
Device devAht{"AHT20", HF_AHT_FAIL};
Device devBmp{"BMP280", HF_BMP_FAIL};

// قيمة لم تتغير إطلاقاً لفترة طويلة = مستشعر عالق
struct StuckWatch {
  float    last = NAN;
  uint32_t since = 0;
  bool stuck(float v, uint32_t now) {
    if (v != last) { last = v; since = now; return false; }
    return now - since > cfg::SENSOR_STUCK_MS;
  }
  void reset() { last = NAN; }   // بعد إعادة التهيئة يبدأ العد من جديد
};

// ================== الساعة ==================
RTC_PCF8563 rtc;
DateTime    lastGood;           // آخر قراءة صحيحة
uint32_t    lastGoodMs = 0;     // millis() عندها — الساعة الاحتياطية
bool        haveGood = false;
bool        clockUnset = false; // علامة فقدان الوقت في PCF8563
uint32_t    lastRepairMs = 0;
bool        repairedEver = false;
uint32_t    repairs = 0;

// ================== AHT20 (تعريف مباشر بلا انتظار) ==================
// مكتبة Adafruit تنتظر بحلقة while بلا مهلة؛ لو فُصلت القطعة يتجمد الجهاز.
// هنا: أمر القياس ثم القراءة بعد 80ms في دورة لاحقة.
constexpr uint8_t AHT_STATUS_BUSY = 0x80;
constexpr uint8_t AHT_STATUS_CAL  = 0x08;
bool       ahtMeasuring = false;
uint32_t   ahtTrigMs = 0;
StuckWatch ahtStuck;

// ================== BMP280 ==================
Adafruit_BMP280 bmp(&Wire);
uint8_t    bmpAddr = 0;
StuckWatch bmpStuck;

// ================== الاهتزاز ==================
volatile uint32_t vibEdges = 0;
portMUX_TYPE      vibMux = portMUX_INITIALIZER_UNLOCKED;
bool              vibPrint = false;

void IRAM_ATTR vibIsr() { vibEdges = vibEdges + 1; }

// ================== الجدولة ==================
uint32_t lastClockMs = 0, lastEnvMs = 0, lastVibMs = 0;
uint8_t  busFailCycles = 0;

// ---------------------------------------------------------------
bool writeBytes(uint8_t addr, const uint8_t* data, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

bool readBytes(uint8_t addr, uint8_t* buf, size_t len) {
  if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

// ================== I2C: تحرير الناقل ==================
void busRecovery() {
  LOG_W("I2C bus recovery");
  Wire.end();
  pinMode(cfg::PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(cfg::PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9 && digitalRead(cfg::PIN_I2C_SDA) == LOW; i++) {
    digitalWrite(cfg::PIN_I2C_SCL, LOW);  delayMicroseconds(5);
    digitalWrite(cfg::PIN_I2C_SCL, HIGH); delayMicroseconds(5);
  }
  Wire.begin(cfg::PIN_I2C_SDA, cfg::PIN_I2C_SCL, cfg::I2C_CLOCK_HZ);
  Wire.setTimeOut(cfg::I2C_TIMEOUT_MS);
  model::setHealth(HF_I2C_RECOVERED, true);

  // كل القطع تُعاد تهيئتها فوراً
  devRtc.retryNow();
  devAht.retryNow();
  devBmp.retryNow();
  ahtMeasuring = false;
}

// فشل كل القطع معاً عدة دورات = الناقل نفسه عالق
void checkBus() {
  bool anyAlive = i2cProbe(cfg::ADDR_OLED) || i2cProbe(cfg::ADDR_RTC) ||
                  i2cProbe(cfg::ADDR_AHT20) || (bmpAddr && i2cProbe(bmpAddr));
  if (anyAlive) { busFailCycles = 0; return; }
  if (++busFailCycles >= cfg::I2C_FAILS_FOR_BUS_RECOVERY) {
    busFailCycles = 0;
    busRecovery();
  }
}

// ================== الساعة ==================
bool sane(const DateTime& t) {
  return t.isValid() && t.year() >= 2024 && t.year() <= 2099;
}

// وقت الترجمة يُطبق مرة واحدة لكل نسخة كود جديدة (يُحفظ في NVS)
void applyBuildTimeOnce() {
  if (millis() > cfg::BUILD_TIME_MAX_UPTIME_MS) return;
  DateTime build(F(__DATE__), F(__TIME__));
  uint32_t buildId = build.unixtime();

  Preferences prefs;
  if (!prefs.begin("hsc", false)) return;
  if (prefs.getUInt("buildTs", 0) != buildId) {
    // الإزاحة تعوض زمن الرفع، ونضيف الوقت منذ الإقلاع
    DateTime t = build + TimeSpan(cfg::BUILD_TIME_OFFSET_S + (int32_t)(millis() / 1000));
    rtc.adjust(t);
    // لا نحفظ إلا بعد التأكد أن الكتابة وصلت، وإلا نحاول في الإقلاع التالي
    if (!rtc.lostPower() && sane(rtc.now())) {
      prefs.putUInt("buildTs", buildId);
      LOG_I("RTC set from build time: %04d-%02d-%02d %02d:%02d:%02d",
            t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
    } else {
      LOG_W("RTC build-time write not confirmed");
    }
  }
  prefs.end();
}

bool rtcInit() {
  if (!i2cProbe(cfg::ADDR_RTC) || !rtc.begin(&Wire)) return false;
  applyBuildTimeOnce();
  if (!rtc.isrunning()) rtc.start();
  return true;
}

// الإصلاح الذاتي: الساعة فقدت الوقت أثناء التشغيل (هبوط جهد لحظي) والوقت الصحيح معروف
// من الساعة الداخلية (آخر قراءة صحيحة + millis) ← يُكتب فيها من جديد بدل عرض --:--.
// لا يعمل إن فُقد الوقت والجهاز مطفأ (لا وقت صحيح معروف في هذا التشغيل): هنا يلزم SET.
bool repairFromBackup(uint32_t now) {
  repairedEver = true;
  lastRepairMs = now;

  DateTime t = lastGood + TimeSpan((int32_t)((now - lastGoodMs) / 1000));
  rtc.adjust(t);
  if (rtc.lostPower()) {
    LOG_E("RTC lost time and repair failed");
    return false;
  }
  repairs++;
  lastGood = t;
  lastGoodMs = now;
  LOG_W("RTC lost time -> restored from internal clock %02d:%02d:%02d", t.hour(), t.minute(), t.second());
  return true;
}

void readClock(uint32_t now) {
  if (!devRtc.ok && devRtc.canRetry(now)) {
    if (rtcInit()) devRtc.markOk(); else devRtc.markFail(now);
  }

  if (devRtc.ok) {
    if (!i2cProbe(cfg::ADDR_RTC)) {
      devRtc.markFail(now);
    } else {
      DateTime t = rtc.now();
      bool lost = rtc.lostPower();
      if (lost) {
        // علامة فقدان الوقت في الساعة نفسها: إصلاح ذاتي إن كان الوقت الصحيح معروفاً، وإلا = غير مضبوط.
        // بين محاولتين (دقيقة) تكمل الساعة الداخلية العرض.
        if (!haveGood || clockUnset) clockUnset = true;
        else if (!repairedEver || now - lastRepairMs >= cfg::RTC_REPAIR_GAP_MS) {
          if (!repairFromBackup(now)) clockUnset = true;
        }
      } else if (sane(t)) {
        lastGood = t;
        lastGoodMs = now;
        haveGood = true;
        clockUnset = false;
      } else {
        devRtc.errors++;     // قراءة واحدة تالفة (تشويش): تُتجاهل والساعة الاحتياطية تكمل
      }
    }
  }

  // الساعة الاحتياطية: آخر وقت صحيح + millis()
  ClockTime ct;
  if (haveGood) {
    DateTime t = lastGood + TimeSpan((int32_t)((now - lastGoodMs) / 1000));
    ct.year = t.year(); ct.month = t.month(); ct.day = t.day();
    ct.hour = t.hour(); ct.minute = t.minute(); ct.second = t.second();
    ct.dow = t.dayOfTheWeek();
    ct.valid = !clockUnset;
    ct.fromBackup = !devRtc.ok;
  }
  model::update([&](Snapshot& s) {
    s.clock = ct;
    if (clockUnset) s.health |= HF_CLOCK_UNSET; else s.health &= ~HF_CLOCK_UNSET;
  });
}

// ================== AHT20 ==================
bool ahtInit() {
  if (!i2cProbe(cfg::ADDR_AHT20)) return false;
  uint8_t st;
  if (!readBytes(cfg::ADDR_AHT20, &st, 1)) return false;
  if (!(st & AHT_STATUS_CAL)) {
    const uint8_t init[] = {0xBE, 0x08, 0x00};
    if (!writeBytes(cfg::ADDR_AHT20, init, sizeof(init))) return false;
    delay(10);
  }
  ahtMeasuring = false;
  return true;
}

uint8_t ahtCrc(const uint8_t* d, size_t n) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
  }
  return crc;
}

bool ahtTrigger() {
  const uint8_t cmd[] = {0xAC, 0x33, 0x00};
  return writeBytes(cfg::ADDR_AHT20, cmd, sizeof(cmd));
}

bool ahtRead(float& tempC, float& hum) {
  uint8_t d[7];
  if (!readBytes(cfg::ADDR_AHT20, d, 7)) return false;
  if (d[0] & AHT_STATUS_BUSY) return false;
  if (ahtCrc(d, 6) != d[6]) return false;

  uint32_t h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
  uint32_t t = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
  hum   = h * 100.0f / 1048576.0f;
  tempC = t * 200.0f / 1048576.0f - 50.0f;
  return true;
}

void ahtFail(uint32_t now) {
  devAht.markFail(now);
  ahtMeasuring = false;
  model::update([](Snapshot& s) {
    s.cabinTemp.fail(ReadErr::DeviceFail);
    s.humidity.fail(ReadErr::DeviceFail);
  });
}

// الطور 1: أمر القياس مع دورة البيئة
void ahtStart(uint32_t now) {
  if (!devAht.ok) {
    if (!devAht.canRetry(now)) return;
    if (ahtInit()) { devAht.markOk(); ahtStuck.reset(); } else { ahtFail(now); return; }
  }
  if (ahtTrigger()) { ahtMeasuring = true; ahtTrigMs = now; }
  else ahtFail(now);
}

// الطور 2: القراءة بعد 80ms
void ahtFinish(uint32_t now) {
  if (!ahtMeasuring || now - ahtTrigMs < cfg::AHT_MEASURE_MS) return;
  ahtMeasuring = false;

  float t, h;
  if (!ahtRead(t, h)) { ahtFail(now); return; }
  if (ahtStuck.stuck(t + h * 1000.0f, now)) {
    LOG_W("AHT20 value stuck");
    ahtFail(now);
    model::update([](Snapshot& s) { s.cabinTemp.fail(ReadErr::Stuck); s.humidity.fail(ReadErr::Stuck); });
    return;
  }
  model::update([&](Snapshot& s) {
    s.cabinTemp.set(t, cfg::RANGE_TEMP_MIN, cfg::RANGE_TEMP_MAX, now);
    s.humidity.set(h, cfg::RANGE_HUM_MIN, cfg::RANGE_HUM_MAX, now);
  });
}

// ================== BMP280 ==================
bool bmpInit() {
  for (uint8_t a : {cfg::ADDR_BMP280_A, cfg::ADDR_BMP280_B}) {
    if (i2cProbe(a) && bmp.begin(a)) { bmpAddr = a; return true; }
  }
  return false;
}

void bmpFail(uint32_t now, ReadErr e = ReadErr::DeviceFail) {
  devBmp.markFail(now);
  model::update([=](Snapshot& s) { s.pressure.fail(e); });
}

void bmpReadCycle(uint32_t now) {
  if (!devBmp.ok) {
    if (!devBmp.canRetry(now)) return;
    if (bmpInit()) { devBmp.markOk(); bmpStuck.reset(); } else { bmpFail(now); return; }
  }
  if (!i2cProbe(bmpAddr)) { bmpFail(now); return; }

  float p = bmp.readPressure() / 100.0f;
  if (bmpStuck.stuck(p, now)) { LOG_W("BMP280 value stuck"); bmpFail(now, ReadErr::Stuck); return; }
  model::update([&](Snapshot& s) { s.pressure.set(p, cfg::RANGE_PRESS_MIN, cfg::RANGE_PRESS_MAX, now); });
}

// ================== الاهتزاز ==================
void vibWindow() {
  portENTER_CRITICAL(&vibMux);
  uint32_t edges = vibEdges;
  vibEdges = 0;
  portEXIT_CRITICAL(&vibMux);

  uint16_t c = edges > 0xFFFF ? 0xFFFF : (uint16_t)edges;
  model::update([=](Snapshot& s) { s.vibCount = c; });
  if (vibPrint) Serial.printf("[VIB] %u%s\n", c, c >= cfg::VIB_STRONG_COUNT ? "  STRONG" : "");
}

}  // namespace

// ================== الواجهة العامة ==================
bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void begin() {
  pinMode(cfg::PIN_VIBRATION, INPUT);
  attachInterrupt(digitalPinToInterrupt(cfg::PIN_VIBRATION), vibIsr, CHANGE);

  uint32_t now = millis();
  if (rtcInit()) devRtc.markOk(); else devRtc.markFail(now);
  if (ahtInit()) devAht.markOk(); else devAht.markFail(now);
  if (bmpInit()) devBmp.markOk(); else devBmp.markFail(now);

  readClock(now);
  lastClockMs = lastEnvMs = lastVibMs = now;
  ahtStart(now);
}

void loop() {
  uint32_t now = millis();

  if (now - lastVibMs >= cfg::VIB_WINDOW_MS) {
    lastVibMs = now;
    vibWindow();
  }

  ahtFinish(now);

  if (now - lastClockMs >= cfg::RTC_READ_MS) {
    lastClockMs = now;
    checkBus();
    readClock(now);
  }

  if (now - lastEnvMs >= cfg::ENV_READ_MS) {
    lastEnvMs = now;
    ahtStart(now);
    bmpReadCycle(now);
  }
}

bool setTime(const DateTime& t) {
  if (!sane(t)) return false;
  if (!devRtc.ok && !rtcInit()) return false;
  rtc.adjust(t);
  haveGood = false;
  readClock(millis());
  LOG_I("RTC set: %04d-%02d-%02d %02d:%02d:%02d  lostPower=%s",
        t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second(),
        rtc.lostPower() ? "YES" : "no");
  return true;
}

void setVibMonitor(bool on) { vibPrint = on; }
bool vibMonitor() { return vibPrint; }

void printStatus(Print& out) {
  Snapshot s;
  if (!model::read(s)) { out.println("model busy"); return; }

  const Device* devs[] = {&devRtc, &devAht, &devBmp};
  out.println("----- Devices -----");
  for (const Device* d : devs)
    out.printf("  %-7s %-5s errors=%lu\n", d->name, d->ok ? "OK" : "FAIL", (unsigned long)d->errors);

  out.println("----- Values -----");
  const ClockTime& c = s.clock;
  out.printf("  Clock    %04d-%02d-%02d %02d:%02d:%02d  valid=%s%s  auto-repairs=%lu\n",
                c.year, c.month, c.day, c.hour, c.minute, c.second,
                c.valid ? "yes" : "NO (unset)", c.fromBackup ? "  [backup]" : "",
                (unsigned long)repairs);

  auto pr = [&](const char* name, const Reading& r, const char* unit) {
    if (r.fresh(cfg::ENV_STALE_MS)) out.printf("  %-8s %.1f %s\n", name, r.value, unit);
    else out.printf("  %-8s --  (err %d)\n", name, (int)r.err);
  };
  pr("Cabin", s.cabinTemp, "C");
  pr("Humidity", s.humidity, "%");
  pr("Pressure", s.pressure, "hPa");
  out.printf("  Vibration %u edges/%lums\n", s.vibCount, (unsigned long)cfg::VIB_WINDOW_MS);
  out.printf("  Health   0x%04X\n", s.health);
}

}  // namespace sensors
