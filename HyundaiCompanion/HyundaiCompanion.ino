// ===== Hyundai Smart Companion — مساعد السيارة الذكي =====
// التهيئة، جدولة المهام، التنسيق بين الوحدات، أوامر Serial، سجل التشخيص.
// المسار المحلي (هذه الحلقة) يعمل دائماً؛ مسار OBD مهمة مستقلة لا تنتظرها الحلقة أبداً.

#include <Wire.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "data_model.h"
#include "sensors.h"
#include "display.h"
#include "audio.h"
#include "obd.h"
#include "alerts.h"
#include "ble_console.h"

// ================== سبب آخر إعادة تشغيل ==================
static const char* resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic/crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (low voltage)";
    case ESP_RST_DEEPSLEEP:return "deep sleep";
    default:               return "other";
  }
}

static bool isAbnormalReset(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
         r == ESP_RST_WDT   || r == ESP_RST_BROWNOUT;
}

// ================== سجل التشخيص (يبقى بعد إطفاء الجهاز) ==================
// يتجمع عبر المشاوير: عدد مرات التشغيل، إعادة التشغيل غير الطبيعية وسببها،
// مجموع مدة التشغيل، أطول تشغيل متواصل، وأقل ذاكرة حرة وصل لها الجهاز.
// الكتابة في الذاكرة كل 5 دقائق فقط (لا تستهلك الفلاش).
namespace diag {

uint32_t boots = 0, abnormal = 0, runMin = 0, maxRunMin = 0, minHeap = UINT32_MAX;
uint8_t  lastAbn = 0;
uint32_t lastSaveMs = 0;

void begin(esp_reset_reason_t rr) {
  Preferences p;
  if (!p.begin("diag", false)) return;
  boots     = p.getUInt("boots", 0) + 1;
  abnormal  = p.getUInt("abn", 0);
  lastAbn   = p.getUChar("lastAbn", 0);
  runMin    = p.getUInt("runMin", 0);
  maxRunMin = p.getUInt("maxRun", 0);
  minHeap   = p.getUInt("minHeap", UINT32_MAX);
  if (isAbnormalReset(rr)) {
    abnormal++;
    lastAbn = (uint8_t)rr;
    p.putUInt("abn", abnormal);
    p.putUChar("lastAbn", lastAbn);
  }
  p.putUInt("boots", boots);
  p.end();
}

void loop() {
  uint32_t now = millis();
  if (now - lastSaveMs < cfg::DIAG_SAVE_MS) return;
  runMin += (now - lastSaveMs) / 60000;
  lastSaveMs = now;
  if (now / 60000 > maxRunMin) maxRunMin = now / 60000;
  if (ESP.getMinFreeHeap() < minHeap) minHeap = ESP.getMinFreeHeap();

  Preferences p;
  if (!p.begin("diag", false)) return;
  p.putUInt("runMin", runMin);
  p.putUInt("maxRun", maxRunMin);
  p.putUInt("minHeap", minHeap);
  p.end();
}

void print(Print& out) {
  out.println("----- Diagnostics (all trips) -----");
  out.printf("  Firmware  %s  (built %s %s)\n", cfg::FW_VERSION, __DATE__, __TIME__);
  out.printf("  Boots     %lu\n", (unsigned long)boots);
  out.printf("  Abnormal  %lu", (unsigned long)abnormal);
  if (abnormal) out.printf("  (last: %s)", resetReasonText((esp_reset_reason_t)lastAbn));
  out.println();
  out.printf("  Run time  %lu h %02lu min total, longest %lu h %02lu min, this run %lu min\n",
                (unsigned long)(runMin / 60), (unsigned long)(runMin % 60),
                (unsigned long)(maxRunMin / 60), (unsigned long)(maxRunMin % 60),
                (unsigned long)(millis() / 60000));
  uint32_t mh = ESP.getMinFreeHeap() < minHeap ? ESP.getMinFreeHeap() : minHeap;
  out.printf("  Memory    min ever=%lu  now free=%u  largest block=%u\n", (unsigned long)mh,
                ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  // أقل مساحة مكدس بقيت منذ التشغيل؛ أقل من ~500 بايت = خطر
  out.printf("  Stack     loop free=%u  obd free=%lu bytes\n",
                uxTaskGetStackHighWaterMark(NULL), (unsigned long)obd::stackFree());
}

void reset() {
  Preferences p;
  if (p.begin("diag", false)) { p.clear(); p.end(); }
  boots = 1; abnormal = 0; lastAbn = 0; runMin = 0; maxRunMin = 0; minHeap = UINT32_MAX;
  lastSaveMs = millis();
}

}  // namespace diag

// ================== Watchdog ==================
// الحلقة الرئيسية تُغذّي المراقب تلقائياً كل دورة؛ تعليق 10 ث = إعادة تشغيل
static void watchdogBegin() {
  esp_task_wdt_config_t wdt = {
    .timeout_ms = cfg::WATCHDOG_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&wdt);
  enableLoopWDT();
}

// ================== أوامر Serial (ومن الجوال عبر BLE) ==================
//   SET 2026-10-21 17:45:00   ضبط الساعة
//   TIME                      الوقت الحالي
//   STATUS                    حالة القطع والقيم
//   VIB                       تشغيل/إيقاف طباعة الاهتزاز (للمعايرة)
//   PLAY 25                   تشغيل الملف 0025.mp3
//   TONE 1|2|3                نغمة مستوى التنبيه
//   SAY HELLO|TIME|DATE       جملة منطوقة
//   TESTSND                   كل ملفات الجدول بالترتيب
//   VOL 0..30 | STOP
//   OBD                       حالة OBD والقيم
//   OBDRAW                    تشغيل/إيقاف طباعة كل أمر ورده
//   OBD 0105                  أمر يدوي للقراءة فقط (AT.., 01.., 09..)
//   OBDGAP 150                الفاصل بين أوامر ELM (ms)
//   ALERTS                    حالة التنبيهات والجلسة
//   ALERTTEST 1..6            عرض تنبيه ونغمته للتجربة: 1 ماء، 2 سرعة، 3 اهتزاز، 4 حر، 5 برد، 6 استراحة
//   SESSION 29                تقديم الجلسة (دقائق) لتجربة تذكير الاستراحة
//   DIAG | DIAG RESET         سجل التشخيص عبر كل المشاوير / تصفيره
//   LOG 0..3                  مستوى السجلات (0 بدون، 3 كل شيء)
//   HELP
static void printHelp(Print& o = Serial) {
  o.printf("Hyundai Smart Companion %s\n", cfg::FW_VERSION);
  o.println("Commands: SET YYYY-MM-DD HH:MM:SS | TIME | STATUS | DIAG | DIAG RESET | LOG 0-3 | VIB | HELP");
  o.println("Audio:    PLAY n | TONE 1-3 | SAY HELLO/TIME/DATE | TESTSND | VOL n | STOP");
  o.println("OBD:      OBD | OBDRAW | OBD <cmd> | OBDGAP ms");
  o.println("Alerts:   ALERTS | ALERTTEST 1-6 | SESSION min");
}

static ClockTime currentClock() {
  Snapshot s;
  model::read(s);
  return s.clock;
}

static void sayCommand(const char* what) {
  ClockTime c = currentClock();
  bool ok = strcmp(what, "HELLO") == 0 ? audio::sayGreeting(c)
          : strcmp(what, "TIME")  == 0 ? audio::sayTime(c)
          : strcmp(what, "DATE")  == 0 ? audio::sayDate(c)
          : false;
  ble_console::out().println(ok ? "OK" : "ERROR: audio not ready, clock unset, or use SAY HELLO|TIME|DATE");
}

static void handleCommand(char* line) {
  Print& o = ble_console::out();   // الرد إلى Serial والجوال
  for (char* p = line; *p; p++) *p = toupper(*p);

  int y, mo, d, h, mi, s;
  if (strncmp(line, "SET ", 4) == 0) {
    if (sscanf(line + 4, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6 &&
        mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h >= 0 && h <= 23 &&
        mi >= 0 && mi <= 59 && s >= 0 && s <= 59 &&
        sensors::setTime(DateTime(y, mo, d, h, mi, s))) {
      o.printf("OK  %04d-%02d-%02d %02d:%02d:%02d\n", y, mo, d, h, mi, s);
    } else {
      o.println("ERROR: use SET 2026-10-21 17:45:00");
    }
  } else if (strcmp(line, "TIME") == 0) {
    ClockTime c = currentClock();
    if (c.valid) o.printf("%04d-%02d-%02d %02d:%02d:%02d\n", c.year, c.month, c.day, c.hour, c.minute, c.second);
    else o.println("clock not set: use SET 2026-10-21 17:45:00");
  } else if (strcmp(line, "STATUS") == 0) {
    sensors::printStatus(o);
    audio::printStatus(o);
    obd::printStatus(o);
    alerts::printStatus(o);
    o.printf("  Heap     free=%u  min=%u  largest=%u  uptime=%lu min\n", ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned long)(millis() / 60000));
  } else if (strcmp(line, "DIAG") == 0) {
    diag::print(o);
  } else if (strcmp(line, "DIAG RESET") == 0) {
    diag::reset();
    o.println("diagnostics cleared");
  } else if (sscanf(line, "LOG %d", &y) == 1 && y >= 0 && y <= 3) {
    cfg::logLevel = y;
    o.printf("log level %d\n", y);
  } else if (strcmp(line, "ALERTS") == 0) {
    alerts::printStatus(o);
  } else if (sscanf(line, "ALERTTEST %d", &y) == 1 && y >= 1 && y <= 6) {
    alerts::test((alerts::Id)y);
    o.println("OK");
  } else if (sscanf(line, "SESSION %d", &y) == 1 && y >= 0 && y <= 600) {
    alerts::fakeSession(y);
    o.printf("session set to %d min\n", y);
  } else if (strcmp(line, "OBD") == 0) {
    obd::printStatus(o);
  } else if (strcmp(line, "OBDRAW") == 0) {
    obd::setRawMonitor(!obd::rawMonitor());
    o.printf("OBD raw monitor %s\n", obd::rawMonitor() ? "ON" : "OFF");
  } else if (sscanf(line, "OBDGAP %d", &y) == 1 && y >= 0 && y <= 1000) {
    obd::setCmdGap(y);
    o.printf("OBD gap %d ms\n", y);
  } else if (strncmp(line, "OBD ", 4) == 0) {
    o.println(obd::sendRaw(line + 4) ? "OK (reply follows)"
                                          : "ERROR: not connected, or command not allowed (read-only: AT/01/02/03/07/09)");
  } else if (sscanf(line, "PLAY %d", &y) == 1 && y > 0 && y < 10000) {
    uint16_t id = y;
    o.println(audio::play(audio::Prio::Info, &id, 1) ? "OK" : "ERROR: audio not ready");
  } else if (sscanf(line, "TONE %d", &y) == 1 && y >= 1 && y <= 3) {
    o.println(audio::tone((audio::Prio)y) ? "OK" : "ERROR: audio not ready");
  } else if (strncmp(line, "SAY ", 4) == 0) {
    sayCommand(line + 4);
  } else if (strcmp(line, "TESTSND") == 0) {
    audio::testAll();
  } else if (sscanf(line, "VOL %d", &y) == 1 && y >= 0 && y <= 30) {
    audio::setVolume(y);
    o.println("OK");
  } else if (strcmp(line, "STOP") == 0) {
    audio::stopAll();
    o.println("OK");
  } else if (strcmp(line, "VIB") == 0) {
    sensors::setVibMonitor(!sensors::vibMonitor());
    o.printf("VIB monitor %s\n", sensors::vibMonitor() ? "ON" : "OFF");
  } else {
    printHelp(o);
  }
}

// قراءة سطر كامل في مخزن ثابت (بدون String)
static void pollSerial() {
  static char buf[48];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (len) { buf[len] = 0; handleCommand(buf); len = 0; }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

// أمر من الجوال (BLE): يُنفذ هنا في الحلقة الرئيسية، لا في مهمة البلوتوث
static void pollPhone() {
  char line[64];
  if (ble_console::takeLine(line, sizeof(line))) handleCommand(line);
}

// ================== مراقبة الذاكرة ==================
// نقص مستمر دقيقة ← إعادة تشغيل منظمة، والسيارة واقفة فقط (لا تنطفئ الشاشة أثناء القيادة)
static void checkMemory() {
  static uint32_t last = 0, lowSince = 0;
  uint32_t now = millis();
  if (now - last < 5000) return;
  last = now;

  bool low = ESP.getFreeHeap() < cfg::MIN_FREE_HEAP;
  model::setHealth(HF_LOW_MEMORY, low);
  if (!low) { lowSince = 0; return; }
  if (!lowSince) { lowSince = now; LOG_W("low memory: %u", ESP.getFreeHeap()); }

  Snapshot s;
  bool moving = model::read(s) && s.speed.fresh(cfg::OBD_STALE_MS, now) && s.speed.value >= cfg::DRIVE_EXIT_KMH;
  if (now - lowSince >= cfg::LOW_HEAP_RESTART_MS && !moving) {
    LOG_E("low memory for %lu s -> restart", (unsigned long)((now - lowSince) / 1000));
    Serial.flush();
    ESP.restart();
  }
}

// ================== النطق المجدول ==================
// الترحيب عند التشغيل إن جهزت القطعة والساعة، وإلا يكمل بصمت.
// في وضع TimeHourly: الوقت بعد الترحيب ثم كل ساعة.
static void speechSchedule() {
  static bool    greeted = false;
  static int8_t  lastHour = -1;

  ClockTime c = currentClock();

  if (!greeted) {
    if (audio::ready() && c.valid) {
      audio::sayGreeting(c);                          // صباح الخير يا عبد العزيز
      if (cfg::SAY_DATE_WITH_GREETING) audio::sayDate(c);  // اليوم هو.. تاريخ اليوم هو..
      if (cfg::SPEAK_MODE == cfg::SpeakMode::TimeHourly) audio::sayTime(c);
      greeted = true;
      lastHour = c.hour;
      alerts::releaseStartup();   // التنبيهات تبدأ بعد انتهاء الترحيب
    } else if (millis() > cfg::GREETING_CLOCK_WAIT_MS) {
      greeted = true;   // فات وقت الترحيب
      alerts::releaseStartup();
    }
    return;
  }

  if (cfg::SPEAK_MODE == cfg::SpeakMode::TimeHourly && c.valid &&
      c.minute == 0 && c.hour != lastHour) {
    lastHour = c.hour;
    audio::sayTime(c);
  }
}

// ================== SETUP / LOOP ==================
void setup() {
  Serial.begin(cfg::SERIAL_BAUD);
  delay(300);

  esp_reset_reason_t rr = esp_reset_reason();
  LOG_I("Hyundai Smart Companion %s — core %d.%d.%d, reset: %s", cfg::FW_VERSION,
        ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH,
        resetReasonText(rr));
  diag::begin(rr);

  if (!model::begin()) LOG_E("model mutex create failed");
  model::setHealth(HF_ABNORMAL_RESET, isAbnormalReset(rr));

  Wire.begin(cfg::PIN_I2C_SDA, cfg::PIN_I2C_SCL, cfg::I2C_CLOCK_HZ);
  Wire.setTimeOut(cfg::I2C_TIMEOUT_MS);
  LOG_I("I2C clock %lu Hz", (unsigned long)Wire.getClock());

  display::begin();
  sensors::begin();
  audio::begin();
  alerts::begin();
  watchdogBegin();
  obd::begin();      // بعد الـ Watchdog: مهمة OBD تسجل نفسها فيه
  ble_console::begin();   // اتصال الجوال (BLE) بعد تهيئة البلوتوث

  printHelp();
}

void loop() {
  sensors::loop();
  audio::loop();
  alerts::loop();
  speechSchedule();
  display::loop();
  pollSerial();
  pollPhone();
  checkMemory();
  diag::loop();
  ble_console::loop();
}
