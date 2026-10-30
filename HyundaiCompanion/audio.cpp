// ===== audio.cpp =====
// تعريف DFPlayer مباشر بلا انتظار. مكتبة DFRobotDFPlayerMini تنتظر ~2 ث في begin()
// و500ms في كل استعلام، وأوامرها الخام غير متاحة. الإطارات هنا نفس إطارات المكتبة.
#include "audio.h"
#include "config.h"

namespace audio {
namespace {

HardwareSerial dfSerial(2);

// ================== أوامر وأحداث DFPlayer ==================
constexpr uint8_t CMD_VOLUME     = 0x06;
constexpr uint8_t CMD_RESET      = 0x0C;
constexpr uint8_t CMD_PLAY_MP3   = 0x12;   // /mp3/NNNN.mp3
constexpr uint8_t CMD_STOP       = 0x16;
constexpr uint8_t EVT_USB_DONE   = 0x3C;
constexpr uint8_t EVT_SD_DONE    = 0x3D;
constexpr uint8_t EVT_ONLINE     = 0x3F;
constexpr uint8_t EVT_INSERTED   = 0x3A;
constexpr uint8_t EVT_REMOVED    = 0x3B;
constexpr uint8_t EVT_ERROR      = 0x40;
constexpr uint8_t EVT_ACK        = 0x41;
constexpr uint8_t Q_SD_FILES     = 0x48;   // عدد الملفات على البطاقة

constexpr uint16_t ERR_BUSY          = 1;  // البطاقة تُهيأ أو غير موجودة
constexpr uint16_t ERR_INDEX_OUT     = 5;
constexpr uint16_t ERR_FILE_MISMATCH = 6;  // الملف غير موجود

// ================== حالة القطعة ==================
enum class Df : uint8_t { WaitBoot, Query, Ready, Fail };
const char* const DF_NAMES[] = {"booting", "querying", "ready", "FAIL"};

Df       df = Df::WaitBoot;
uint32_t dfStateMs = 0;
uint32_t resetMs = 0;
uint32_t lastTxMs = 0;
uint32_t lastHealthMs = 0;
bool     queryPending = false;
uint32_t queryMs = 0;
bool     cardOk = false;
uint16_t fileCount = 0;
uint8_t  volume = cfg::DF_VOLUME;
uint32_t errMissing = 0, errOther = 0, errFrame = 0, timeouts = 0;
uint16_t lastFlags = 0xFFFF;
uint8_t  healthMisses = 0;     // ردود فحص الصحة الضائعة المتتالية

// ================== الطابور ==================
struct Utterance {
  Prio     prio = Prio::Info;
  uint8_t  count = 0;
  uint16_t clips[cfg::AUDIO_MAX_CLIPS];
  uint32_t queuedMs = 0;
};

Utterance cur;
bool      playing = false;
uint8_t   curIdx = 0;
uint32_t  clipStartMs = 0;

Utterance queue[cfg::AUDIO_QUEUE_SIZE];
uint8_t   qLen = 0;

bool     testMode = false;
uint16_t testId = 0;

// ================== الإرسال ==================
// إطار: 7E FF 06 CMD ACK PH PL CHK_H CHK_L EF  (ACK=0: لا ننتظر تأكيداً)
void send(uint8_t cmd, uint16_t arg = 0) {
  uint32_t since = millis() - lastTxMs;
  if (since < cfg::DF_CMD_GAP_MS) delay(cfg::DF_CMD_GAP_MS - since);  // ≤30ms

  uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0x00, (uint8_t)(arg >> 8), (uint8_t)arg, 0, 0, 0xEF};
  uint16_t sum = 0;
  for (int i = 1; i < 7; i++) sum += f[i];
  sum = 0 - sum;
  f[7] = sum >> 8;
  f[8] = sum & 0xFF;
  dfSerial.write(f, sizeof(f));
  lastTxMs = millis();
}

void query(uint8_t q) {
  send(q);
  queryPending = true;
  queryMs = millis();
}

// ================== الصحة ==================
void updateHealth() {
  uint16_t flags = 0;
  if (df == Df::Fail) flags |= HF_AUDIO_FAIL;
  if (df == Df::Ready && !cardOk) flags |= HF_SD_MISSING;
  if (flags == lastFlags) return;
  lastFlags = flags;
  model::update([=](Snapshot& s) {
    s.health = (s.health & ~(HF_AUDIO_FAIL | HF_SD_MISSING)) | flags;
  });
}

void setState(Df s) {
  if (s != df) LOG_I("DFPlayer %s", DF_NAMES[(int)s]);
  df = s;
  dfStateMs = millis();
  updateHealth();
}

void abortPlayback() {
  playing = false;
  qLen = 0;
  testMode = false;
}

void startReset() {
  send(CMD_RESET);
  resetMs = millis();
  queryPending = false;
  healthMisses = 0;
  cardOk = false;
  abortPlayback();
  setState(Df::WaitBoot);
}

void fail(const char* why) {
  LOG_W("DFPlayer: %s", why);
  timeouts++;
  queryPending = false;
  abortPlayback();
  setState(Df::Fail);
}

// ================== التشغيل ==================
void playClip(uint16_t id) {
  send(CMD_PLAY_MP3, id);
  clipStartMs = millis();
}

void startUtterance(const Utterance& u) {
  cur = u;
  curIdx = 0;
  playing = true;
  playClip(cur.clips[0]);
}

// ---------- وضع الاختبار: كل ملفات الجدول بالترتيب ----------
struct Range { uint16_t from, to; };
constexpr Range TEST_RANGES[] = {
  {1, 6}, {10, 12}, {40, 46}, {50, 54}, {61, 72}, {81, 92}, {101, 159}, {226, 228}, {301, 331},
};

uint16_t nextTestId(uint16_t prev) {
  for (const Range& r : TEST_RANGES) {
    if (prev < r.from) return r.from;
    if (prev < r.to) return prev + 1;
  }
  return 0;
}

void advanceTest() {
  testId = nextTestId(testId);
  if (!testId) { testMode = false; Serial.println("[TEST] done"); return; }
  Serial.printf("[TEST] %04u\n", testId);
  Utterance u;
  u.count = 1;
  u.clips[0] = testId;
  startUtterance(u);
}

void nextClip() {
  if (++curIdx < cur.count) { playClip(cur.clips[curIdx]); return; }
  playing = false;
  if (testMode) advanceTest();
}

// الأعلى أولوية أولاً، وبالترتيب بين المتساوية. المعلومة القديمة تُحذف.
void startNextFromQueue() {
  uint32_t now = millis();
  uint8_t w = 0;
  for (uint8_t i = 0; i < qLen; i++) {
    bool expired = queue[i].prio == Prio::Info && now - queue[i].queuedMs > cfg::AUDIO_INFO_MAX_WAIT_MS;
    if (expired) LOG_I("audio: info expired");
    else queue[w++] = queue[i];
  }
  qLen = w;
  if (!qLen) return;

  uint8_t best = 0;
  for (uint8_t i = 1; i < qLen; i++)
    if (queue[i].prio < queue[best].prio) best = i;

  Utterance u = queue[best];
  for (uint8_t i = best; i + 1 < qLen; i++) queue[i] = queue[i + 1];
  qLen--;
  startUtterance(u);
}

// ================== الاستقبال ==================
void handleFrame(uint8_t cmd, uint16_t param) {
  switch (cmd) {
    case EVT_ACK:
      break;

    case EVT_ONLINE:                         // بعد reset، أو إعادة تشغيل القطعة نفسها (انخفاض جهد)
      if (param & 0x02) cardOk = true;
      // القطعة ترجع لمستواها الافتراضي بعد إعادة تشغيلها
      if (df == Df::Ready) { send(CMD_VOLUME, volume); updateHealth(); }
      break;

    case Q_SD_FILES:
      queryPending = false;
      healthMisses = 0;
      fileCount = param;
      cardOk = param > 0;
      if (df == Df::Query || df == Df::WaitBoot) {
        send(CMD_VOLUME, volume);
        setState(Df::Ready);
        LOG_I("DFPlayer files on SD: %u", fileCount);
      }
      lastHealthMs = millis();
      updateHealth();
      break;

    case EVT_SD_DONE:
    case EVT_USB_DONE:
      // الحدث المكرر يصل مباشرة بعد بدء المقطع التالي
      if (playing && millis() - clipStartMs > cfg::DF_DUP_FINISH_MS) nextClip();
      break;

    case EVT_INSERTED:
      LOG_I("SD inserted");
      startReset();                         // القطعة تعيد فهرسة البطاقة
      break;

    case EVT_REMOVED:
      LOG_W("SD removed");
      cardOk = false;
      abortPlayback();
      updateHealth();
      break;

    case EVT_ERROR:
      // "مشغول" أثناء الإقلاع = البطاقة ما زالت تُفهرس؛ يُعاد الاستعلام بعد المهلة
      if (queryPending && param == ERR_BUSY && df != Df::Ready) break;
      if (queryPending) {                   // استعلام فشل = لا بطاقة
        queryPending = false;
        cardOk = false;
        if (df != Df::Ready) { send(CMD_VOLUME, volume); setState(Df::Ready); }
        updateHealth();
      }
      if (param == ERR_FILE_MISMATCH || param == ERR_INDEX_OUT) {
        errMissing++;
        if (playing) LOG_W("missing file %04u.mp3", cur.clips[curIdx]);
      } else {
        errOther++;
        LOG_W("DFPlayer error %u", param);
      }
      if (playing) nextClip();              // يُتجاوز المقطع، والتنبيه المرئي يبقى
      break;

    default:
      break;
  }
}

uint8_t rx[10];
uint8_t rxLen = 0;

void pollRx() {
  while (dfSerial.available()) {
    uint8_t b = dfSerial.read();
    if (rxLen == 0 && b != 0x7E) continue;  // مزامنة على بداية الإطار
    rx[rxLen++] = b;
    if (rxLen < sizeof(rx)) continue;
    rxLen = 0;

    uint16_t sum = 0;
    for (int i = 1; i < 7; i++) sum += rx[i];
    uint16_t chk = ((uint16_t)rx[7] << 8) | rx[8];
    if (rx[9] != 0xEF || (uint16_t)(sum + chk) != 0) { errFrame++; continue; }
    handleFrame(rx[3], ((uint16_t)rx[5] << 8) | rx[6]);
  }
}

}  // namespace

// ================== الواجهة العامة ==================
void begin() {
  dfSerial.begin(cfg::DF_BAUD, SERIAL_8N1, cfg::PIN_DF_RX, cfg::PIN_DF_TX);
  startReset();
}

void loop() {
  pollRx();
  uint32_t now = millis();

  switch (df) {
    case Df::WaitBoot:
      if (now - dfStateMs >= cfg::DF_RESET_WAIT_MS) {
        query(Q_SD_FILES);
        setState(Df::Query);
      }
      break;

    case Df::Query:
      if (queryPending && now - queryMs > cfg::DF_QUERY_TIMEOUT_MS) {
        if (now - resetMs < cfg::DF_BOOT_MAX_MS) query(Q_SD_FILES);   // ما زالت تُفهرس البطاقة
        else fail("no response");
      }
      break;

    case Df::Fail:
      if (now - dfStateMs >= cfg::DFPLAYER_RETRY_MS) startReset();
      break;

    case Df::Ready:
      // رد واحد ضائع قد يكون تشويشاً: يُعاد الاستعلام، والفشل بعد ردّين متتاليين
      if (queryPending && now - queryMs > cfg::DF_QUERY_TIMEOUT_MS) {
        if (++healthMisses >= 2) { healthMisses = 0; fail("no response"); }
        else query(Q_SD_FILES);
        break;
      }
      if (!playing && !queryPending && now - lastHealthMs >= cfg::DF_HEALTH_MS) {
        lastHealthMs = now;
        query(Q_SD_FILES);                 // يكشف غياب القطعة أو البطاقة
        break;
      }
      if (playing && now - clipStartMs > cfg::DF_CLIP_MAX_MS) {
        LOG_W("clip timeout");
        nextClip();
      }
      if (!playing && cardOk) startNextFromQueue();
      break;
  }
}

bool play(Prio p, const uint16_t* clips, uint8_t count) {
  if (!ready() || count == 0) return false;
  if (count > cfg::AUDIO_MAX_CLIPS) count = cfg::AUDIO_MAX_CLIPS;

  Utterance u;
  u.prio = p;
  u.count = count;
  memcpy(u.clips, clips, count * sizeof(uint16_t));
  u.queuedMs = millis();

  // الأعلى أولوية يقطع الحالي
  if (playing && p < cur.prio) {
    testMode = false;
    startUtterance(u);
    return true;
  }
  if (!playing && qLen == 0) { startUtterance(u); return true; }

  if (qLen < cfg::AUDIO_QUEUE_SIZE) { queue[qLen++] = u; return true; }

  // الطابور ممتلئ: يحل محل الأدنى أولوية (الأحدث بينها) إن كان أدنى منه
  int worst = -1;
  for (int i = 0; i < qLen; i++)
    if (worst < 0 || queue[i].prio >= queue[worst].prio) worst = i;
  if (queue[worst].prio > p) { queue[worst] = u; return true; }
  return false;
}

bool tone(Prio p) {
  uint16_t id = p == Prio::Critical ? snd::TONE_HIGH
              : p == Prio::Warning  ? snd::TONE_MID
                                    : snd::TONE_LOW;
  return play(p, &id, 1);
}

// "صباح الخير يا عبد العزيز" — ملف واحد
bool sayGreeting(const ClockTime& c) {
  if (!c.valid) return false;
  bool morning = c.hour >= cfg::MORNING_FROM_HOUR && c.hour < cfg::EVENING_FROM_HOUR;
  const uint16_t clip = snd::greeting(morning);
  return play(Prio::Info, &clip, 1);
}

// "الساعة الثالثة" + "و خمس دقائق مساءً" — ملفان، ورأس الساعة ملف واحد
bool sayTime(const ClockTime& c) {
  if (!c.valid || c.minute > 59) return false;
  uint16_t clips[3];
  uint8_t n = 0;
  uint8_t h = c.hour % 12;
  if (h == 0) h = 12;
  bool pm = c.hour >= 12;

  if (cfg::SAY_TIME_PREFIX) clips[n++] = snd::W_TIME_NOW;
  if (c.minute > 0) {
    clips[n++] = snd::hour12(h);
    clips[n++] = snd::andMinute(c.minute, pm);
  } else {
    clips[n++] = snd::hourOnly(h, pm);
  }
  return play(Prio::Info, clips, n);
}

// "اليوم هو الجمعة" + "تاريخ اليوم هو الحادي والعشرون" + "من أكتوبر 2026" — 3 ملفات
bool sayDate(const ClockTime& c) {
  if (!c.valid || c.day < 1 || c.day > 31 || c.month < 1 || c.month > 12) return false;
  const uint16_t clips[] = {snd::todayIs(c.dow % 7), snd::dateIs(c.day), snd::monthYear(c.month, c.year)};
  return play(Prio::Info, clips, 3);
}

void stopAll() {
  if (df == Df::Ready) send(CMD_STOP);
  abortPlayback();
}

void testAll() {
  if (!ready()) { Serial.println("[TEST] DFPlayer not ready"); return; }
  stopAll();
  testMode = true;
  testId = 0;
  advanceTest();
}

void setVolume(uint8_t v) {
  volume = v > 30 ? 30 : v;
  if (df == Df::Ready) send(CMD_VOLUME, volume);
}

bool ready() { return df == Df::Ready && cardOk; }
bool idle()  { return !playing && qLen == 0; }

void printStatus(Print& out) {
  out.println("----- Audio -----");
  out.printf("  DFPlayer %s  card=%s  files=%u  volume=%u\n",
                DF_NAMES[(int)df], cardOk ? "OK" : "MISSING", fileCount, volume);
  if (playing) out.printf("  Playing  %04u (%u/%u) prio=%u\n",
                             cur.clips[curIdx], curIdx + 1, cur.count, (unsigned)cur.prio);
  else out.println("  Playing  -");
  out.printf("  Queue    %u   missing=%lu other=%lu frame=%lu timeouts=%lu\n", qLen,
                (unsigned long)errMissing, (unsigned long)errOther,
                (unsigned long)errFrame, (unsigned long)timeouts);
}

}  // namespace audio
