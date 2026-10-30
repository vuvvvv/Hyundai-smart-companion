// ===== obd.cpp =====
// مبني على كود الاختبار الناجح elm.ino، بعد تحويل String إلى مخازن ثابتة.
#include "obd.h"
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <esp_gap_bt_api.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "data_model.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED) || !defined(CONFIG_BT_SPP_ENABLED)
#error "Bluetooth Classic (SPP) غير متوفر في هذه اللوحة"
#endif

namespace obd {
namespace {

BluetoothSerial SerialBT;
TaskHandle_t    taskHandle = nullptr;

// ================== الحالة ==================
const char* const LINK_NAMES[] = {"disconnected", "connecting", "initializing", "active", "car asleep", "backoff"};

volatile ObdLink link = ObdLink::Disconnected;   // تُقرأ من الحلقة الرئيسية أيضاً
uint8_t  backoffIdx = 0;
uint8_t  consecErrors = 0;     // أخطاء متتالية (لا رد، ?، STOPPED)
uint8_t  consecNoData = 0;     // NO DATA متتالية = السيارة نائمة
uint8_t  protoFails = 0;       // فشل البروتوكول المحفوظ
bool     needInit = true;
bool     protoConfirmed = false;
char     proto = 0;            // البروتوكول المحفوظ في NVS ('6' = CAN 11-bit 500k)
Support  ambient = Support::Unknown;

uint32_t errorsTotal = 0, connects = 0, reinits = 0;
uint32_t lastCmdMs = 0;
uint32_t lastBattMs = 0, lastCoolMs = 0, lastAmbMs = 0, lastProbeMs = 0;
bool     speedTurn = true;     // السرعة والـ RPM بالتناوب

volatile bool     rawPrint = false;
volatile uint16_t gapMs = cfg::OBD_CMD_GAP_MS;

// أمر يدوي من Serial ينتظر التنفيذ داخل المهمة
portMUX_TYPE  rawMux = portMUX_INITIALIZER_UNLOCKED;
char          rawCmd[24];
volatile bool rawPending = false;

// رد ELM في مخزن ثابت: الأسطر مفصولة بـ '|'
constexpr size_t RESP_SIZE = 160;
char resp[RESP_SIZE];

enum class Res : uint8_t { Ok, NoData, Timeout, Error };
const char* const RES_NAMES[] = {"ok", "no data", "timeout", "error"};

// ================== إخفاء الجهاز ==================
// الجهاز غير مرئي وغير قابل للاتصال: هو من يبدأ الاتصال بالقطعة، فلا يحتاج اتصالات واردة.
// مكتبة BluetoothSerial تجعله مرئياً في حدث التهيئة (يصل بعد begin) وفي بعض دوالها،
// لذلك يُطبق الإخفاء بعد ذلك الحدث، ويُعاد بعد كل محاولة اتصال.
void hide() {
  esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
}

// يُستدعى بعد معالج المكتبة لنفس الحدث
void sppEvent(esp_spp_cb_event_t event, esp_spp_cb_param_t*) {
  if (event == ESP_SPP_INIT_EVT) hide();
}

// ---------------------------------------------------------------
void sleepMs(uint32_t ms) {
  esp_task_wdt_reset();
  vTaskDelay(pdMS_TO_TICKS(ms));
  esp_task_wdt_reset();
}

void drain() {
  while (SerialBT.available()) SerialBT.read();
}

// قيم OBD تصبح غير صالحة عند مغادرة حالة "نشط"؛ الجهد من القطعة نفسها يبقى ما دام البلوتوث متصلاً
void setLink(ObdLink l) {
  if (l != link) LOG_I("OBD %s", LINK_NAMES[(int)l]);
  link = l;
  bool keepBattery = (l == ObdLink::Active || l == ObdLink::CarAsleep || l == ObdLink::Initializing);
  model::update([=](Snapshot& s) {
    s.obdLink = l;
    if (l != ObdLink::Active) {
      s.speed.fail(ReadErr::NoData);
      s.rpm.fail(ReadErr::NoData);
      s.coolant.fail(ReadErr::NoData);
      s.ambient.fail(ReadErr::NoData);
    }
    if (!keepBattery) s.battery.fail(ReadErr::NoData);
  });
}

// ================== إرسال أمر وانتظار '>' ==================
// الأمر و\r في حزمة واحدة، مع فاصل زمني قابل للضبط بين الأوامر (هذا ما أنهى مشكلة "?" في الاختبار)
Res command(const char* cmd, uint32_t timeoutMs = cfg::OBD_CMD_TIMEOUT_MS) {
  uint32_t since = millis() - lastCmdMs;
  if (since < gapMs) sleepMs(gapMs - since);
  drain();

  char out[32];
  int n = snprintf(out, sizeof(out), "%s\r", cmd);
  SerialBT.write((const uint8_t*)out, n);

  size_t len = 0;
  bool prompt = false;
  uint32_t start = millis();
  while (!prompt && millis() - start < timeoutMs) {
    while (SerialBT.available()) {
      int c = SerialBT.read();
      if (c == '>') { prompt = true; break; }
      if (c == '\r' || c == '\n') {
        if (len && resp[len - 1] != '|' && len < RESP_SIZE - 1) resp[len++] = '|';
      } else if (c > 0 && len < RESP_SIZE - 1) {
        resp[len++] = toupper(c);
      }
    }
    if (!prompt) { esp_task_wdt_reset(); vTaskDelay(1); }
  }
  while (len && (resp[len - 1] == '|' || resp[len - 1] == ' ')) len--;
  resp[len] = 0;
  lastCmdMs = millis();

  Res r;
  if (!prompt) r = Res::Timeout;
  else if (strstr(resp, "NO DATA") || strstr(resp, "UNABLE TO CONNECT") ||
           strstr(resp, "CAN ERROR") || strstr(resp, "BUS INIT")) r = Res::NoData;
  else if (strchr(resp, '?') || strstr(resp, "STOPPED") || strstr(resp, "ERROR")) r = Res::Error;
  else r = Res::Ok;

  if (rawPrint) Serial.printf("[OBD] %-6s -> %s  (%s)\n", cmd, resp, RES_NAMES[(int)r]);
  return r;
}

// يكرر الأمر حتى يرد OK
bool expectOk(const char* cmd, int tries = 4) {
  for (int i = 0; i < tries; i++) {
    if (command(cmd) == Res::Ok && strstr(resp, "OK")) return true;
    sleepMs(300);
  }
  LOG_W("OBD %s: no OK", cmd);
  return false;
}

// ================== فك ردود PID ==================
uint8_t hexNibble(char c) { return c <= '9' ? c - '0' : c - 'A' + 10; }

// يجد "41XX" في الرد (بعد حذف المسافات) ويقرأ البايتات بعده. يرجع عددها.
int pidBytes(const char* pid, uint8_t* out, int maxBytes) {
  char c[RESP_SIZE];
  size_t j = 0;
  for (size_t i = 0; resp[i] && j < sizeof(c) - 1; i++)
    if (resp[i] != ' ') c[j++] = resp[i];
  c[j] = 0;

  const char key[5] = {'4', '1', pid[2], pid[3], 0};
  const char* p = strstr(c, key);
  if (!p) return 0;
  p += 4;
  int n = 0;
  while (n < maxBytes && isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1])) {
    out[n++] = (hexNibble(p[0]) << 4) | hexNibble(p[1]);
    p += 2;
  }
  return n;
}

// نتيجة كل أمر: عدادات الأخطاء وNO DATA
void track(Res r) {
  if (r == Res::Ok) { consecErrors = 0; consecNoData = 0; return; }
  if (r == Res::NoData) { consecNoData++; return; }
  consecErrors++;
  errorsTotal++;
  model::update([](Snapshot& s) { s.obdErrors = errorsTotal; });
}

// ================== القراءات ==================
// رد صحيح الصيغة لكنه لا يحتوي البيانات المتوقعة = خطأ
Res readPid(const char* pid, int needBytes, uint8_t* b) {
  Res r = command(pid);
  if (r == Res::Ok && pidBytes(pid, b, 4) < needBytes) r = Res::Error;
  track(r);
  return r;
}

// سرعة OBD الحقيقية ← سرعة مطابقة لعداد السيارة (تُستخدم في الشاشة والتنبيهات معاً)
float dashSpeed(float v) {
  float ramp = v >= cfg::SPEED_OFFSET_FULL_AT_KMH ? 1.0f : v / cfg::SPEED_OFFSET_FULL_AT_KMH;
  return v + cfg::SPEED_OFFSET_KMH * ramp;
}

void readSpeed() {
  uint8_t b[4];
  if (readPid("010D", 1, b) != Res::Ok) return;
  float v = dashSpeed(b[0]);
  uint32_t now = millis();
  model::update([&](Snapshot& s) { s.speed.set(v, cfg::RANGE_SPEED_MIN, cfg::RANGE_SPEED_MAX, now); });
}

void readRpm() {
  uint8_t b[4];
  if (readPid("010C", 2, b) != Res::Ok) return;
  float rpm = ((b[0] << 8) | b[1]) / 4.0f;
  uint32_t now = millis();
  model::update([&](Snapshot& s) { s.rpm.set(rpm, cfg::RANGE_RPM_MIN, cfg::RANGE_RPM_MAX, now); });
}

void readCoolant() {
  uint8_t b[4];
  if (readPid("0105", 1, b) != Res::Ok) return;
  uint32_t now = millis();
  model::update([&](Snapshot& s) { s.coolant.set(b[0] - 40.0f, cfg::RANGE_COOLANT_MIN, cfg::RANGE_COOLANT_MAX, now); });
}

void readAmbient() {
  uint8_t b[4];
  if (readPid("0146", 1, b) != Res::Ok) return;
  uint32_t now = millis();
  model::update([&](Snapshot& s) { s.ambient.set(b[0] - 40.0f, cfg::RANGE_TEMP_MIN, cfg::RANGE_TEMP_MAX, now); });
}

// ATRV: جهد البطارية من القطعة نفسها، لا يرسل شيئاً للسيارة
void readBattery() {
  Res r = command("ATRV");
  float v = NAN;
  if (r == Res::Ok) {
    v = atof(resp);
    if (v <= 0) r = Res::Error;
  }
  if (r == Res::NoData) r = Res::Error;   // ATRV لا يعتمد على السيارة
  track(r);
  if (r != Res::Ok) return;
  uint32_t now = millis();
  model::update([&](Snapshot& s) { s.battery.set(v, cfg::RANGE_BATT_MIN, cfg::RANGE_BATT_MAX, now); });
}

// 0140: هل تدعم السيارة 0146 (حرارة الهواء الخارجي)؟ يُفحص مرة واحدة
void detectAmbient() {
  if (ambient != Support::Unknown) return;
  uint8_t b[4];
  Res r = command("0140");
  if (r == Res::Ok && pidBytes("0140", b, 4) >= 1) ambient = (b[0] & 0x04) ? Support::Yes : Support::No;
  else if (r == Res::NoData) ambient = Support::No;
  else return;   // خطأ: نحاول في الجلسة التالية
  LOG_I("OBD ambient temp (0146): %s", ambient == Support::Yes ? "supported" : "not supported");
  model::update([](Snapshot& s) { s.ambientSupport = ambient; });
}

// ================== التهيئة والبروتوكول ==================
bool initElm() {
  setLink(ObdLink::Initializing);
  reinits++;
  // القطع المقلدة بطيئة في إعادة التشغيل: ننتظر ثم ننظف
  command("ATZ", 5000);
  sleepMs(cfg::OBD_RESET_WAIT_MS);
  drain();
  const char* const setup[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATAT1"};
  for (const char* c : setup)
    if (!expectOk(c)) return false;
  consecErrors = 0;
  return true;
}

void saveProto(char p) {
  Preferences prefs;
  if (!prefs.begin("hsc", false)) return;
  prefs.putChar("obdProto", p);
  prefs.end();
}

// البروتوكول المحفوظ مباشرة؛ إن فشل مرتين ← اكتشاف تلقائي ATSP0 ثم ATDPN ثم تثبيته وحفظه
Res detectProtocol() {
  char spCmd[8];
  if (proto && protoFails < cfg::OBD_PROTO_FAILS_FOR_AUTO) {
    snprintf(spCmd, sizeof(spCmd), "ATSP%c", proto);
    if (expectOk(spCmd)) {
      Res r = command("0100", 5000);
      if (r == Res::Ok && strstr(resp, "4100")) { protoFails = 0; protoConfirmed = true; detectAmbient(); return Res::Ok; }
      if (r == Res::NoData) return Res::NoData;   // السيارة مطفأة، والبروتوكول قد يكون صحيحاً
    }
    protoFails++;
    LOG_W("OBD saved protocol %c failed (%u)", proto, protoFails);
    return Res::Error;
  }

  if (!expectOk("ATSP0")) return Res::Error;
  Res r = command("0100", cfg::OBD_DETECT_TIMEOUT_MS);   // البحث قد يأخذ عدة ثوانٍ
  if (r != Res::Ok || !strstr(resp, "4100")) return r == Res::NoData ? Res::NoData : Res::Error;

  if (command("ATDPN") == Res::Ok) {
    // "A6" = تلقائي ووجد 6
    char p = resp[0] == 'A' ? resp[1] : resp[0];
    if (isxdigit((unsigned char)p)) {
      snprintf(spCmd, sizeof(spCmd), "ATSP%c", p);
      if (expectOk(spCmd)) {
        if (p != proto) { saveProto(p); LOG_I("OBD protocol %c saved", p); }
        proto = p;
      }
    }
  }
  protoFails = 0;
  protoConfirmed = true;
  detectAmbient();
  return Res::Ok;
}

// ================== الجلسة ==================
void activeStep() {
  uint32_t now = millis();
  if (now - lastBattMs >= cfg::OBD_BATTERY_MS)                         { lastBattMs = now; readBattery(); }
  else if (now - lastCoolMs >= cfg::OBD_COOLANT_MS)                    { lastCoolMs = now; readCoolant(); }
  else if (ambient == Support::Yes && now - lastAmbMs >= cfg::OBD_AMBIENT_MS) { lastAmbMs = now; readAmbient(); }
  else { if (speedTurn) readSpeed(); else readRpm(); speedTurn = !speedTurn; }

  if (consecNoData >= cfg::OBD_NODATA_FOR_SLEEP) {
    // البلوتوث سليم لكن السيارة لا ترد: نتوقف عن أوامر PIDs حتى لا نوقظ وحدات التحكم
    consecNoData = 0;
    lastProbeMs = millis();
    setLink(ObdLink::CarAsleep);
  }
}

void asleepStep() {
  uint32_t now = millis();
  if (now - lastBattMs >= cfg::OBD_SLEEP_KEEPALIVE_MS) {
    lastBattMs = now;
    readBattery();                 // keep-alive: يبقي القطعة مشغولة ولا يلمس السيارة
    return;
  }
  if (now - lastProbeMs >= cfg::OBD_SLEEP_PROBE_MS) {
    lastProbeMs = now;
    if (!protoConfirmed) {         // لم نعرف البروتوكول في هذه الجلسة بعد
      Res r = detectProtocol();
      if (r == Res::Ok) setLink(ObdLink::Active);
      else if (r == Res::Error) track(r);
      return;
    }
    uint8_t b[4];
    if (readPid("010C", 2, b) == Res::Ok) {
      LOG_I("OBD car awake");
      setLink(ObdLink::Active);
    }
    return;
  }
  sleepMs(100);
}

bool validRaw(const char* c) {
  size_t n = strlen(c);
  if (n < 2 || n > 20) return false;
  for (size_t i = 0; i < n; i++)
    if (!isalnum((unsigned char)c[i]) && c[i] != ' ') return false;
  // ATMA يغرق الاتصال ببيانات مستمرة، وATPP يكتب إعدادات دائمة في القطعة نفسها
  if (strncmp(c, "ATMA", 4) == 0 || strncmp(c, "ATPP", 4) == 0) return false;
  // قراءة فقط: AT، الوضع 01 (بيانات حية)، 02، 03 (قراءة الأعطال)، 07، 09 (معلومات السيارة)
  static const char* const allowed[] = {"AT", "01", "02", "03", "07", "09"};
  for (const char* a : allowed)
    if (strncmp(c, a, 2) == 0) return true;
  return false;
}

void handleRaw() {
  if (!rawPending) return;
  char c[sizeof(rawCmd)];
  portENTER_CRITICAL(&rawMux);
  strlcpy(c, rawCmd, sizeof(c));
  rawPending = false;
  portEXIT_CRITICAL(&rawMux);

  Res r = command(c, 5000);
  Serial.printf("[OBD RAW] %s -> %s  (%s)\n", c, resp, RES_NAMES[(int)r]);
}

// متصل بالبلوتوث: تهيئة، ثم قراءة، حتى ينقطع الاتصال أو تفشل التهيئة مرتين
void runSession() {
  needInit = true;
  protoConfirmed = false;
  uint8_t initFails = 0;

  while (SerialBT.connected()) {
    esp_task_wdt_reset();

    if (needInit) {
      Res r = initElm() ? detectProtocol() : Res::Error;
      if (r == Res::Error) {
        if (++initFails >= cfg::OBD_REINIT_FAILS_FOR_DROP) { LOG_W("OBD init failed twice"); return; }
        sleepMs(1000);
        continue;
      }
      initFails = 0;
      needInit = false;
      consecErrors = consecNoData = 0;
      lastBattMs = lastCoolMs = lastAmbMs = 0;
      lastProbeMs = millis();
      setLink(r == Res::Ok ? ObdLink::Active : ObdLink::CarAsleep);
      backoffIdx = 0;              // جلسة ناجحة: المهلة القادمة تبدأ من الأول
    } else if (link == ObdLink::Active) {
      activeStep();
    } else {
      asleepStep();
    }

    handleRaw();

    // أخطاء متتالية ← إعادة التهيئة (ATZ) من غير قطع البلوتوث
    if (consecErrors >= cfg::OBD_ERRORS_FOR_REINIT) {
      LOG_W("OBD %u consecutive errors -> reinit", consecErrors);
      consecErrors = 0;
      needInit = true;
    }
  }
  LOG_W("OBD bluetooth lost");
}

// disconnect() ينتظر حتى 10 ث حدث الإغلاق، وهذا يساوي مهلة الـ Watchdog: نخرج منه أثناءه
void btDisconnect() {
  esp_task_wdt_delete(NULL);
  SerialBT.disconnect();
  esp_task_wdt_add(NULL);
  hide();
}

bool connectElm() {
  setLink(ObdLink::Connecting);
  uint8_t mac[6];
  memcpy(mac, cfg::OBD_MAC, sizeof(mac));
  BTAddress addr(mac);
  // connect() قد ينتظر حتى 20 ث: المهمة تخرج من الـ Watchdog أثناءه فقط
  esp_task_wdt_delete(NULL);
  bool ok = SerialBT.connect(addr, cfg::OBD_CHANNEL, ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER);
  esp_task_wdt_add(NULL);
  hide();
  if (!ok) { btDisconnect(); return false; }
  connects++;
  sleepMs(500);
  return true;
}

// انتظار متزايد 5 ← 10 ← 20 ← 30 ث كحد أقصى
void waitBackoff() {
  constexpr uint8_t steps = sizeof(cfg::OBD_BACKOFF_MS) / sizeof(cfg::OBD_BACKOFF_MS[0]);
  uint32_t wait = cfg::OBD_BACKOFF_MS[backoffIdx];
  if (backoffIdx < steps - 1) backoffIdx++;
  setLink(ObdLink::Backoff);
  LOG_I("OBD retry in %lu s", (unsigned long)(wait / 1000));
  for (uint32_t t = 0; t < wait; t += 200) sleepMs(200);
}

void obdTask(void*) {
  esp_task_wdt_add(NULL);
  for (;;) {
    if (connectElm()) runSession();
    btDisconnect();
    waitBackoff();
  }
}

}  // namespace

// ================== الواجهة العامة ==================
void begin() {
  Preferences prefs;
  if (prefs.begin("hsc", true)) {
    proto = prefs.getChar("obdProto", 0);
    prefs.end();
  }

  SerialBT.disableSSP();                         // PIN القديم (Legacy) كما في الاختبار
  SerialBT.register_callback(sppEvent);          // قبل begin حتى يُلتقط حدث التهيئة
  // true = Master. ذاكرة BLE تُحرر فقط إن لم يكن اتصال الجوال (BLE) مفعّلاً
  if (!SerialBT.begin(cfg::BT_DEVICE_NAME, true, !cfg::BLE_ENABLED)) {
    LOG_E("Bluetooth init failed — OBD disabled");
    return;
  }
  // setPin يُستخدم في كل اقتران، ومنه الاقتران بقطعة OBD (رمزها 1234).
  // الرمز الخاص (16 رقماً) إن عطّل الاتصال بالقطعة: BT_USE_OWN_PIN = false، والحماية بالإخفاء.
  const char* pin = cfg::BT_USE_OWN_PIN ? cfg::BT_DEVICE_PIN : cfg::OBD_PIN;
  SerialBT.setPin(pin, strlen(pin));
  hide();
  LOG_I("Bluetooth hidden, PIN: %s", cfg::BT_USE_OWN_PIN ? "own (16 digits)" : "OBD module");  // لا يُطبع الرمز نفسه

  xTaskCreatePinnedToCore(obdTask, "obd", cfg::OBD_TASK_STACK, nullptr,
                          cfg::OBD_TASK_PRIORITY, &taskHandle, cfg::OBD_TASK_CORE);
  LOG_I("OBD task started (saved protocol: %c)", proto ? proto : '-');
}

void printStatus(Print& out) {
  Snapshot s;
  if (!model::read(s)) return;
  uint32_t now = millis();
  auto pr = [&](const char* name, const Reading& r, const char* fmt) {
    out.printf("  %-9s", name);
    if (r.fresh(cfg::OBD_STALE_MS, now)) out.printf(fmt, r.value);
    else out.print("--");
    out.println();
  };
  out.println("----- OBD -----");
  out.printf("  Link     %s  protocol=%c  ambient=%s  gap=%ums\n", LINK_NAMES[(int)s.obdLink],
                proto ? proto : '-',
                s.ambientSupport == Support::Yes ? "yes" : s.ambientSupport == Support::No ? "no" : "?",
                gapMs);
  pr("Speed", s.speed, "%.0f km/h");
  pr("RPM", s.rpm, "%.0f");
  pr("Coolant", s.coolant, "%.0f C");
  pr("Ambient", s.ambient, "%.0f C");
  pr("Battery", s.battery, "%.1f V");
  out.printf("  Counters connects=%lu reinits=%lu errors=%lu\n",
                (unsigned long)connects, (unsigned long)reinits, (unsigned long)errorsTotal);
}

void setRawMonitor(bool on) { rawPrint = on; }
bool rawMonitor() { return rawPrint; }

bool sendRaw(const char* cmd) {
  if (link != ObdLink::Active && link != ObdLink::CarAsleep) return false;
  if (!validRaw(cmd)) return false;
  portENTER_CRITICAL(&rawMux);
  strlcpy(rawCmd, cmd, sizeof(rawCmd));
  rawPending = true;
  portEXIT_CRITICAL(&rawMux);
  return true;
}

void setCmdGap(uint16_t ms) { gapMs = ms; }
uint16_t cmdGap() { return gapMs; }
uint32_t stackFree() { return taskHandle ? uxTaskGetStackHighWaterMark(taskHandle) : 0; }

}  // namespace obd
