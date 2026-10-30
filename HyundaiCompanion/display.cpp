// ===== display.cpp =====
// الشاشة ثنائية اللون في العتاد: الصفوف 0..15 صفراء، و16..63 زرقاء (لا تتغير برمجياً).
// الشريط الأصفر: الرموز الصغيرة والعناوين العربية. المنطقة الزرقاء: الأرقام والرموز الكبيرة.
// الهدف: قراءة المعلومة بنظرة واحدة أثناء القيادة، فالرموز بدل الكلمات حيث أمكن.
#include "display.h"
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "config.h"
#include "data_model.h"
#include "sensors.h"
#include "alerts.h"
#include "bitmaps.h"

namespace display {
namespace {

// السرعة 400kHz أثناء الرسم وبعده (الافتراضي في المكتبة يرجع الناقل إلى 100kHz)
Adafruit_SSD1306 oled(cfg::OLED_WIDTH, cfg::OLED_HEIGHT, &Wire, -1,
                      cfg::I2C_CLOCK_HZ, cfg::I2C_CLOCK_HZ);

constexpr int16_t W = cfg::OLED_WIDTH;
constexpr int16_t BAND_H = 16;     // الشريط الأصفر
#define DEG_C "\xF8" "C"           // °C في خط cp437

bool     oledOk = false;
uint32_t lastDrawMs = 0;
uint32_t lastProbeMs = 0;
uint32_t splashUntil = 0;

// وضع القيادة
bool     driveMode = false;
uint32_t enterSince = 0, exitSince = 0, lostSince = 0;

bool oledInit() {
  if (!sensors::i2cProbe(cfg::ADDR_OLED)) return false;
  // periphBegin=false: الناقل مهيأ مسبقاً، لا نعيد Wire.begin()
  if (!oled.begin(SSD1306_SWITCHCAPVCC, cfg::ADDR_OLED, true, false)) return false;
  oled.setTextColor(SSD1306_WHITE);
  oled.cp437(true);
  oled.clearDisplay();
  oled.display();
  return true;
}

// الشاشة مفصولة ← علامة خطأ؛ عادت ← إعادة تهيئة (ربما فقدت إعداداتها)
void checkOled(uint32_t now) {
  if (now - lastProbeMs < cfg::OLED_PROBE_MS) return;
  lastProbeMs = now;

  bool present = sensors::i2cProbe(cfg::ADDR_OLED);
  if (oledOk && !present) {
    oledOk = false;
    LOG_W("OLED lost");
  } else if (!oledOk && present) {
    oledOk = oledInit();
    if (oledOk) LOG_I("OLED OK");
  }
  model::setHealth(HF_OLED_FAIL, !oledOk);
}

// ================== أدوات الرسم ==================
void img(const Bitmap& b, int16_t x, int16_t y, uint16_t color = SSD1306_WHITE) {
  oled.drawBitmap(x, y, b.data, b.w, b.h, color);
}

// صورة في وسط الشريط الأصفر عمودياً
int16_t bandY(const Bitmap& b) { return (BAND_H - b.h) / 2; }

// خط صغير مدمج (6×8)
void small(const char* s, int16_t x, int16_t y) {
  oled.setFont(nullptr);
  oled.setTextSize(1);
  oled.setCursor(x, y);
  oled.print(s);
}

int16_t smallW(const char* s) { return (int16_t)strlen(s) * 6; }

void smallRight(const char* s, int16_t y, int16_t right = W) {
  small(s, right - smallW(s), y);
}

// خط كبير ناعم: baseline = خط الأساس، والنص في وسط المساحة [left, left+areaW)
int16_t bigCentered(const char* s, const GFXfont* font, int16_t baseline, int16_t left, int16_t areaW) {
  int16_t x1, y1;
  uint16_t w, h;
  oled.setFont(font);
  oled.setTextSize(1);
  oled.getTextBounds(s, 0, baseline, &x1, &y1, &w, &h);
  oled.setCursor(left + (areaW - (int16_t)w) / 2 - x1, baseline);
  oled.print(s);
  oled.setFont(nullptr);
  return w;
}

int16_t bigWidth(const char* s, const GFXfont* font) {
  int16_t x1, y1;
  uint16_t w, h;
  oled.setFont(font);
  oled.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  oled.setFont(nullptr);
  return w;
}

void formatTime(const ClockTime& c, char* buf, size_t n) {
  if (!c.valid) { strlcpy(buf, "--:--", n); return; }
  uint8_t h = c.hour;
  if (!cfg::DISPLAY_24H) { h %= 12; if (h == 0) h = 12; }
  snprintf(buf, n, "%d:%02d", h, c.minute);
}

// سطر معلومة صغير: رمز + نص، محاذاة يمين
void infoRight(const Bitmap& icon, const char* text, int16_t y) {
  int16_t x = W - smallW(text);
  small(text, x, y);
  img(icon, x - icon.w - 2, y);
}

// ================== شريط الحالة (يسار الشريط الأصفر) ==================
// السيارة: متصل. القمر: متصل والسيارة مطفأة. السيارة مشطوبة: غير متصل.
// بعدها رموز الأعطال: ساعة غير مضبوطة، تحذير مستشعر، بطاقة مفقودة، صوت معطل، كوب الاستراحة.
// ترجع أول x فارغ بعد الرموز.
int16_t drawStatusIcons(const Snapshot& s) {
  int16_t x = 0;
  auto put = [&](const Bitmap& b) {
    if (x + b.w > 46) return;                // لا تزاحم التاريخ واليوم على اليمين
    img(b, x, bandY(b));
    x += b.w + 3;
  };

  if (s.obdLink == ObdLink::Active) {
    put(bmp::S_CAR);
  } else if (s.obdLink == ObdLink::CarAsleep) {
    put(bmp::S_MOON);
  } else {
    int16_t y = bandY(bmp::S_CAR);
    put(bmp::S_CAR);
    oled.drawLine(0, y + bmp::S_CAR.h + 1, bmp::S_CAR.w - 1, y - 2, SSD1306_WHITE);
  }

  if (s.health & (HF_CLOCK_UNSET | HF_RTC_FAIL)) put(bmp::S_CLOCK);
  if (s.health & (HF_AHT_FAIL | HF_BMP_FAIL | HF_ABNORMAL_RESET | HF_LOW_MEMORY)) put(bmp::S_WARN);
  if (s.health & HF_SD_MISSING) put(bmp::S_SD);
  if (s.health & HF_AUDIO_FAIL) put(bmp::S_MUTE);
  if (alerts::restPending()) put(bmp::S_CUP);
  return x;
}

// ================== الواجهة العادية ==================
//  أصفر  رموز الحالة | التاريخ + اسم اليوم بالعربي
//  أزرق  الوقت بخط كبير
//        🏠 حرارة المقصورة كبيرة | سطران: أهم معلومتين متوفرتين
void drawNormal(const Snapshot& s, uint32_t now) {
  const ClockTime& c = s.clock;
  char buf[16];

  drawStatusIcons(s);
  if (c.valid) {
    const Bitmap& day = bmp::DAY[c.dow % 7];
    img(day, W - day.w, bandY(day));
    snprintf(buf, sizeof(buf), "%02d/%02d", c.day, c.month);
    smallRight(buf, 4, W - day.w - 4);
  }

  // الوقت
  formatTime(c, buf, sizeof(buf));
  bool ampm = c.valid && !cfg::DISPLAY_24H;
  bigCentered(buf, &FreeSansBold18pt7b, 42, 0, ampm ? 112 : W);
  if (ampm) small(c.hour < 12 ? "AM" : "PM", 114, 19);

  // حرارة المقصورة «داخل»
  img(bmp::N_HOUSE, 0, 52);
  oled.setTextSize(2);
  oled.setCursor(11, 48);
  if (s.cabinTemp.fresh(cfg::ENV_STALE_MS, now)) {
    snprintf(buf, sizeof(buf), "%.1f", s.cabinTemp.value);
    oled.print(buf);
    oled.drawCircle(oled.getCursorX() + 2, 49, 1, SSD1306_WHITE);
  } else {
    oled.print("--");
  }

  // أهم معلومتين متوفرتين: مع OBD حرارة الماء والخارج، وبدونه الرطوبة والضغط
  int16_t y = 48;
  auto line = [&](const Bitmap& icon, const char* text) {
    if (y > 56) return;
    infoRight(icon, text, y);
    y += 8;
  };
  if (s.coolant.fresh(cfg::OBD_STALE_MS, now)) {
    snprintf(buf, sizeof(buf), "%.0f" DEG_C, s.coolant.value);
    line(bmp::N_COOLANT, buf);
  }
  if (s.ambient.fresh(cfg::OBD_AMBIENT_MS + cfg::OBD_STALE_MS, now)) {
    snprintf(buf, sizeof(buf), "%.0f" DEG_C, s.ambient.value);
    line(bmp::N_TREE, buf);
  }
  if (s.humidity.fresh(cfg::ENV_STALE_MS, now)) {
    snprintf(buf, sizeof(buf), "%.0f%%", s.humidity.value);
    line(bmp::N_DROP, buf);
  }
  if (s.pressure.fresh(cfg::ENV_STALE_MS, now)) {
    snprintf(buf, sizeof(buf), "%.0fhPa", s.pressure.value);
    line(bmp::N_GAUGE, buf);
  }
}

// ================== واجهة القيادة ==================
//  أصفر  رموز الحالة | الوقت
//  أزرق  السرعة بأكبر خط + km/h
//        RPM | حرارة الماء
void drawDrive(const Snapshot& s, uint32_t now) {
  char buf[16];

  drawStatusIcons(s);
  formatTime(s.clock, buf, sizeof(buf));
  if (s.clock.valid && !cfg::DISPLAY_24H) strlcat(buf, s.clock.hour < 12 ? "AM" : "PM", sizeof(buf));
  smallRight(buf, 4);

  // السرعة: إن انقطعت تظهر -- في نفس المكان الكبير
  if (s.speed.fresh(cfg::OBD_STALE_MS, now)) snprintf(buf, sizeof(buf), "%.0f", s.speed.value);
  else strlcpy(buf, "--", sizeof(buf));
  bigCentered(buf, &FreeSansBold24pt7b, 52, 0, 104);
  small("km/h", 104, 44);

  if (s.rpm.fresh(cfg::OBD_STALE_MS, now)) snprintf(buf, sizeof(buf), "%.0frpm", s.rpm.value);
  else strlcpy(buf, "--rpm", sizeof(buf));
  small(buf, 0, 56);

  if (s.coolant.fresh(cfg::OBD_STALE_MS, now)) {
    snprintf(buf, sizeof(buf), "%.0f" DEG_C, s.coolant.value);
    infoRight(bmp::N_COOLANT, buf, 56);
  }
}

// ================== واجهة التنبيه ==================
//  أصفر  العنوان بالعربي (معكوس للتنبيه الحرج)
//  أزرق  رمز كبير على اليسار | القيمة الحالية (أو أعمدة الشدة للاهتزاز)
struct AlertArt { const Bitmap* title; const Bitmap* icon; };

AlertArt artFor(alerts::Id id) {
  switch (id) {
    case alerts::Id::Coolant:   return {&bmp::T_COOLANT, &bmp::I_COOLANT};
    case alerts::Id::Speed:     return {&bmp::T_SPEED,   &bmp::I_SPEED};
    case alerts::Id::Vibration: return {&bmp::T_VIBRATE, &bmp::I_VIBRATE};
    case alerts::Id::CabinHot:  return {&bmp::T_HOT,     &bmp::I_HOT};
    case alerts::Id::CabinCold: return {&bmp::T_COLD,    &bmp::I_COLD};
    case alerts::Id::Rest:      return {&bmp::T_REST,    &bmp::I_REST};
    default:                    return {nullptr, nullptr};
  }
}

// 5 أعمدة متصاعدة، الممتلئ منها = الشدة
void drawIntensity(uint8_t level, int16_t left) {
  for (int i = 0; i < 5; i++) {
    int16_t h = 8 + i * 6;
    int16_t x = left + i * 15;
    int16_t y = 58 - h;
    if (i < level) oled.fillRect(x, y, 11, h, SSD1306_WHITE);
    else oled.drawRect(x, y, 11, h, SSD1306_WHITE);
  }
}

void drawAlert(const alerts::View& a) {
  AlertArt art = artFor(a.id);
  bool strong = a.critical || a.level == 1;

  // العنوان
  if (strong) oled.fillRect(0, 0, W, BAND_H, SSD1306_WHITE);
  if (art.title) img(*art.title, (W - art.title->w) / 2, bandY(*art.title), strong ? SSD1306_BLACK : SSD1306_WHITE);

  // الرمز
  int16_t left = 0;
  if (art.icon) {
    img(*art.icon, 2, BAND_H + (64 - BAND_H - art.icon->h) / 2);
    left = art.icon->w + 6;
  }

  // القيمة
  if (a.id == alerts::Id::Vibration) { drawIntensity(a.intensity, left + 6); return; }
  int16_t areaW = W - left;
  const GFXfont* font = bigWidth(a.value, &FreeSansBold24pt7b) <= areaW - 4 ? &FreeSansBold24pt7b : &FreeSansBold18pt7b;
  bigCentered(a.value, font, 50, left, areaW);
  if (a.unit[0]) smallRight(a.unit, 56);
}

// ================== الترحيب ==================
//  أزرق  كلمة HYUNDAI في الوسط (الاسم يُنطق فقط ولا يُكتب)
void drawSplash() {
  img(bmp::WORDMARK, (W - bmp::WORDMARK.w) / 2, BAND_H + (64 - BAND_H - bmp::WORDMARK.h) / 2);
}

// ================== الانتقال بين الواجهات ==================
// الدخول: أعلى من 5 كم/س لمدة 2 ث. الخروج: أقل من 3 كم/س لمدة 5 ث.
// انقطاع السرعة أثناء القيادة لا يعيد الشاشة فجأة؛ يخرج فقط بعد انقطاع طويل.
void updateMode(const Snapshot& s, uint32_t now) {
  bool fresh = s.speed.fresh(cfg::OBD_STALE_MS, now);
  float v = s.speed.value;

  if (!driveMode) {
    if (fresh && v > cfg::DRIVE_ENTER_KMH) {
      if (!enterSince) enterSince = now;
      if (now - enterSince >= cfg::DRIVE_ENTER_MS) {
        driveMode = true;
        enterSince = exitSince = lostSince = 0;
        LOG_I("display: drive mode");
      }
    } else {
      enterSince = 0;
    }
    return;
  }

  bool below = fresh && v < cfg::DRIVE_EXIT_KMH;
  if (below) { if (!exitSince) exitSince = now; } else exitSince = 0;
  if (!fresh) { if (!lostSince) lostSince = now; } else lostSince = 0;

  if ((exitSince && now - exitSince >= cfg::DRIVE_EXIT_MS) ||
      (lostSince && now - lostSince >= cfg::DRIVE_NODATA_EXIT_MS)) {
    driveMode = false;
    enterSince = exitSince = lostSince = 0;
    LOG_I("display: normal mode");
  }
}

}  // namespace

// ================== الواجهة العامة ==================
void begin() {
  oledOk = oledInit();
  model::setHealth(HF_OLED_FAIL, !oledOk);
  if (!oledOk) LOG_E("OLED not found");
  lastProbeMs = millis();
  splashUntil = millis() + cfg::SPLASH_MS;
  if (oledOk) { drawSplash(); oled.display(); }
}

void loop() {
  uint32_t now = millis();
  checkOled(now);

  alerts::View a;
  bool alert = alerts::current(a);

  // 4 مرات في الثانية في القيادة أو التنبيه، ومرة في الثانية في الوضع العادي
  uint32_t period = (driveMode || alert) ? cfg::DISPLAY_DRIVE_MS : cfg::DISPLAY_NORMAL_MS;
  if (!oledOk || now - lastDrawMs < period) return;
  lastDrawMs = now;

  Snapshot s;
  if (!model::read(s)) return;
  updateMode(s, now);

  // الترحيب ~3 ث ولا يوقف شيئاً؛ أي تنبيه حرج يقطعه فوراً
  bool splash = (int32_t)(splashUntil - now) > 0 && !(alert && a.critical);

  // التنبيه الحرج يبقى، مع عودة للقيادة بالتناوب حتى تبقى السرعة مرئية
  if (alert && a.critical && driveMode) {
    uint32_t phase = (now - a.sinceMs) % (cfg::CRITICAL_SWAP_MS + cfg::CRITICAL_HIDE_MS);
    alert = phase < cfg::CRITICAL_SWAP_MS;
  }

  oled.clearDisplay();
  if (splash) drawSplash();
  else if (alert) drawAlert(a);
  else if (driveMode) drawDrive(s, now);
  else drawNormal(s, now);
  oled.display();
}

}  // namespace display
