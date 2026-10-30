// ===== config.h — كل الإعدادات في مكان واحد =====
// الحدود، المدد، المنافذ، العناوين، البلوتوث. لا أرقام سحرية في بقية الملفات.
#pragma once
#include <Arduino.h>

namespace cfg {

// ================== المنافذ ==================
constexpr uint8_t PIN_I2C_SDA   = 21;
constexpr uint8_t PIN_I2C_SCL   = 22;
constexpr uint8_t PIN_VIBRATION = 27;
constexpr uint8_t PIN_DF_RX     = 16;   // يستقبل من DFPlayer TX
constexpr uint8_t PIN_DF_TX     = 17;   // يرسل إلى DFPlayer RX عبر 1kΩ

// ================== عناوين I2C ==================
constexpr uint32_t I2C_CLOCK_HZ   = 400000;   // الشاشة تحتاج ~25ms لكل إطار بدل ~100ms
constexpr uint16_t I2C_TIMEOUT_MS = 20;
constexpr uint8_t  ADDR_OLED      = 0x3C;
constexpr uint8_t  ADDR_RTC       = 0x51;
constexpr uint8_t  ADDR_AHT20     = 0x38;
constexpr uint8_t  ADDR_BMP280_A  = 0x77;   // العنوان الفعلي في القطعة (من الفحص)
constexpr uint8_t  ADDR_BMP280_B  = 0x76;   // احتياط

// ================== الشاشة ==================
constexpr uint8_t  OLED_WIDTH         = 128;
constexpr uint8_t  OLED_HEIGHT        = 64;
constexpr uint32_t DISPLAY_NORMAL_MS  = 1000;  // مرة في الثانية
constexpr uint32_t DISPLAY_DRIVE_MS   = 250;   // 4 مرات في الثانية
constexpr uint32_t SPLASH_MS          = 3000;
constexpr uint32_t OLED_PROBE_MS      = 5000;  // فحص وجود الشاشة وإعادة تهيئتها إن عادت
constexpr bool     DISPLAY_24H        = false; // false = 12 ساعة مع AM/PM

// وضع القيادة (الفرق بين الحدين يمنع الوميض)
constexpr int      DRIVE_ENTER_KMH    = 5;
constexpr uint32_t DRIVE_ENTER_MS     = 2000;
constexpr int      DRIVE_EXIT_KMH     = 3;
constexpr uint32_t DRIVE_EXIT_MS      = 5000;
constexpr uint32_t DRIVE_NODATA_EXIT_MS = 15000; // السرعة منقطعة هذه المدة = رجوع للعادية

constexpr uint32_t ALERT_SHOW_MS      = 5000;  // التنبيهات غير الحرجة
constexpr uint32_t CRITICAL_SWAP_MS   = 4000;  // التنبيه الحرج يظهر 4 ث...
constexpr uint32_t CRITICAL_HIDE_MS   = 2000;  // ...ثم القيادة 2 ث حتى تبقى السرعة مرئية

// ================== المستشعرات ==================
constexpr uint32_t ENV_READ_MS        = 2000;  // الحرارة والرطوبة والضغط
constexpr uint32_t ENV_STALE_MS       = 7000;  // أقدم من هذا = --
constexpr uint32_t AHT_MEASURE_MS     = 80;    // زمن قياس AHT20 بعد الأمر
constexpr uint32_t RTC_READ_MS        = 1000;
// ضبط الساعة من وقت الترجمة مرة واحدة لكل نسخة جديدة من الكود.
// الإزاحة تعوض زمن الترجمة والرفع؛ الضبط الدقيق بأمر SET.
constexpr int32_t  BUILD_TIME_OFFSET_S = 30;   // مقاس: ~15 ث بقية الترجمة + ~15 ث الرفع
constexpr uint32_t BUILD_TIME_MAX_UPTIME_MS = 10UL * 60 * 1000;  // بعدها لا يُطبق (الساعة رجعت متأخرة)
constexpr uint32_t RTC_REPAIR_GAP_MS  = 60000;  // أقل فاصل بين محاولتي إصلاح ذاتي للساعة
constexpr uint32_t VIB_WINDOW_MS      = 250;
constexpr uint16_t VIB_STRONG_COUNT   = 24;    // تجريبي، يُعاير في السيارة

// حدود المدى المنطقي (خارجها = قراءة غير صالحة)
constexpr float RANGE_TEMP_MIN     = -40.0f, RANGE_TEMP_MAX     = 85.0f;
constexpr float RANGE_HUM_MIN      =   0.0f, RANGE_HUM_MAX      = 100.0f;
constexpr float RANGE_PRESS_MIN    = 300.0f, RANGE_PRESS_MAX    = 1100.0f;
constexpr float RANGE_COOLANT_MIN  = -40.0f, RANGE_COOLANT_MAX  = 150.0f;
constexpr float RANGE_SPEED_MIN    =   0.0f, RANGE_SPEED_MAX    = 240.0f;
constexpr float RANGE_RPM_MIN      =   0.0f, RANGE_RPM_MAX      = 9000.0f;
constexpr float RANGE_BATT_MIN     =   5.0f, RANGE_BATT_MAX     = 18.0f;

// قيمة ثابتة لفترة طويلة = مستشعر عالق
constexpr uint32_t SENSOR_STUCK_MS    = 10UL * 60 * 1000;

// ================== التعافي الذاتي ==================
// مهلة متزايدة لإعادة تهيئة أي قطعة: 1 ث ثم 5 ثم 30 كحد أقصى
constexpr uint32_t RETRY_BACKOFF_MS[] = {1000, 5000, 30000};
constexpr uint8_t  I2C_FAILS_FOR_BUS_RECOVERY = 3;   // فشل كل الأجهزة معاً
constexpr uint32_t DFPLAYER_RETRY_MS  = 30000;
constexpr uint32_t WATCHDOG_TIMEOUT_S = 10;
constexpr uint32_t MIN_FREE_HEAP      = 20000;       // أقل منها = إعادة تشغيل منظمة والسيارة واقفة
constexpr uint32_t LOW_HEAP_RESTART_MS = 60000;      // نقص مستمر هذه المدة قبل إعادة التشغيل
constexpr uint32_t DIAG_SAVE_MS       = 5UL * 60 * 1000;  // حفظ سجل التشخيص (مدة التشغيل، أقل ذاكرة)

// ================== الصوت ==================
constexpr uint32_t DF_BAUD            = 9600;
constexpr uint8_t  DF_VOLUME          = 30;          // 0..30 (30 = أعلى مستوى)
constexpr uint8_t  AUDIO_QUEUE_SIZE   = 4;
constexpr uint32_t AUDIO_INFO_MAX_WAIT_MS = 10000;   // المعلومة التي تنتظر أكثر تُحذف
constexpr uint8_t  AUDIO_MAX_CLIPS    = 10;          // أطول جملة (مقاطع متتالية)

constexpr uint32_t DF_RESET_WAIT_MS   = 1500;  // بعد أمر reset قبل أول استعلام
constexpr uint32_t DF_QUERY_TIMEOUT_MS = 600;  // مهلة رد الاستعلام الواحد
constexpr uint32_t DF_BOOT_MAX_MS     = 10000; // يُكرر الاستعلام أثناء فهرسة البطاقة، بعدها = غير موجودة
constexpr uint32_t DF_CMD_GAP_MS      = 30;    // أقل فاصل بين أمرين
constexpr uint32_t DF_CLIP_MAX_MS     = 30000; // لم يصل حدث الانتهاء = نعتبره انتهى
constexpr uint32_t DF_DUP_FINISH_MS   = 200;   // بعض القطع ترسل حدث الانتهاء مرتين
constexpr uint32_t DF_HEALTH_MS       = 30000; // فحص دوري للقطعة وهي صامتة

// "الساعة الآن" قبل الساعة؟ ملفات الساعات فيها كلمة "الساعة" أصلاً
constexpr bool     SAY_TIME_PREFIX    = false;
constexpr uint32_t GREETING_CLOCK_WAIT_MS = 15000; // انتظار الساعة وDFPlayer للترحيب، وإلا صمت

// أوضاع النطق
enum class SpeakMode : uint8_t { GreetingOnly, TimeHourly, GreetingManual };
constexpr SpeakMode SPEAK_MODE        = SpeakMode::GreetingOnly;
constexpr bool      SAY_DATE_WITH_GREETING = true;   // الترحيب ثم اليوم والتاريخ عند التشغيل
constexpr uint8_t   MORNING_FROM_HOUR = 4;           // صباح الخير من 4 حتى 11:59
constexpr uint8_t   EVENING_FROM_HOUR = 12;

// ================== البلوتوث ==================
// ESP32 مخفي وغير قابل للاتصال؛ الاسم والرمز طبقة إضافية.
constexpr const char* BT_DEVICE_NAME  = "BT-AUX-27";
constexpr const char* BT_DEVICE_PIN   = "9385277113893419";   // 16 رقماً، غيّره إن أردت
// true = الرمز الخاص. إن لم يعد OBD يتصل بعده: false (رمز القطعة 1234)، والحماية بالإخفاء
constexpr bool        BT_USE_OWN_PIN  = true;

// ================== الاتصال من الجوال (BLE) ==================
// OBD على Classic (مخفي ومشغول بالقطعة)، والجوال على BLE: يظهر بنفس الاسم ويطلب الرمز عند الاقتران.
// الأوامر نفسها أوامر Serial (SET، STATUS، TIME...) والرد يرجع للجوال.
constexpr bool     BLE_ENABLED       = true;
constexpr uint32_t BLE_PASSKEY       = 418756;   // 6 أرقام (أقصى طول في BLE)
constexpr uint32_t BLE_TX_CHUNK_MS   = 8;        // فاصل بين أجزاء الرد (20 بايت لكل جزء)

// قطعة ELM327 (اسمها ورمزها ثابتان في القطعة)
constexpr const char* OBD_NAME        = "OBDII";
constexpr const char* OBD_PIN         = "1234";
constexpr uint8_t     OBD_MAC[6]      = {0x00, 0x10, 0xCC, 0x4F, 0x36, 0x03};
constexpr uint8_t     OBD_CHANNEL     = 1;

// ================== OBD ==================
constexpr uint32_t OBD_STALE_MS        = 3000;   // أقدم من هذا = --
constexpr uint16_t OBD_CMD_GAP_MS      = 150;    // فاصل بين الأوامر (المجرّب في elm.ino)، يُضبط بأمر OBDGAP
constexpr uint32_t OBD_TASK_STACK      = 6144;
constexpr UBaseType_t OBD_TASK_PRIORITY = 1;
constexpr BaseType_t  OBD_TASK_CORE     = 0;     // الحلقة الرئيسية على النواة 1
constexpr uint32_t OBD_CMD_TIMEOUT_MS  = 2000;
constexpr uint32_t OBD_RESET_WAIT_MS   = 1500;   // بعد ATZ
constexpr uint32_t OBD_DETECT_TIMEOUT_MS = 15000; // أول 0100 مع ATSP0

constexpr uint32_t OBD_SPEED_RPM_MS    = 250;    // بالتناوب

// مطابقة عداد السيارة: العداد يُظهر أعلى من السرعة الحقيقية عمداً، وOBD يعطي الحقيقية تقريباً.
// مقاس في السيارة: OBD 53 = العداد 60، وOBD 73 = العداد 80 ← +7.
// الإضافة تتدرج من 0 حتى كاملة عند 20 كم/س (حتى لا تظهر 7 والسيارة واقفة). 0 = السرعة الحقيقية.
constexpr float    SPEED_OFFSET_KMH    = 7.0f;
constexpr float    SPEED_OFFSET_FULL_AT_KMH = 20.0f;
constexpr uint32_t OBD_COOLANT_MS      = 2000;
constexpr uint32_t OBD_AMBIENT_MS      = 30000;
constexpr uint32_t OBD_BATTERY_MS      = 5000;

constexpr uint8_t  OBD_ERRORS_FOR_REINIT = 3;    // أخطاء متتالية في "نشط" ← ATZ
constexpr uint8_t  OBD_REINIT_FAILS_FOR_DROP = 2;
constexpr uint8_t  OBD_PROTO_FAILS_FOR_AUTO  = 2;
constexpr uint8_t  OBD_NODATA_FOR_SLEEP  = 3;    // NO DATA متكرر = السيارة نائمة
constexpr uint32_t OBD_SLEEP_KEEPALIVE_MS = 3000;  // ATRV
constexpr uint32_t OBD_SLEEP_PROBE_MS     = 10000; // 010C
constexpr uint32_t OBD_BACKOFF_MS[]    = {5000, 10000, 20000, 30000};

// ================== التنبيهات ==================
constexpr float    COOLANT_ALERT_C     = 105.0f;
constexpr float    COOLANT_REARM_C     = 100.0f;
constexpr uint8_t  COOLANT_CONFIRM     = 3;      // قراءات متتالية
constexpr uint32_t COOLANT_REPEAT_MS   = 5UL * 60 * 1000;

constexpr int      SPEED_ALERT_KMH     = 80;
constexpr int      SPEED_REARM_KMH     = 75;
constexpr uint8_t  SPEED_CONFIRM       = 2;
constexpr uint32_t SPEED_MIN_GAP_MS    = 60000;

constexpr uint32_t VIB_ALERT_GAP_MS    = 10000;
// اهتزاز الطريق أثناء القيادة كثير: حد آخر أعلى أثناء الحركة. 0xFFFF = متوقف حتى المعايرة (أمر VIB)
constexpr uint16_t VIB_STRONG_COUNT_MOVING = 0xFFFF;
constexpr int      VIB_MOVING_KMH      = 5;

constexpr uint32_t ALERT_TICK_MS       = 100;    // فحص التنبيهات 10 مرات في الثانية
constexpr uint32_t ALERT_STARTUP_MAX_MS = 25000; // أقصى انتظار للترحيب قبل تفعيل التنبيهات (15 ث انتظار + الترحيب)

constexpr float    CABIN_HOT_C         = 40.0f;
constexpr float    CABIN_HOT_REARM_C   = 38.0f;
constexpr float    CABIN_COLD_C        = 15.0f;
constexpr float    CABIN_COLD_REARM_C  = 17.0f;

// ================== جلسة القيادة ==================
constexpr uint32_t SESSION_START_RPM_MS = 10000;          // RPM > 0 لمدة 10 ث
constexpr uint32_t SESSION_BREAK_MS     = 10UL * 60 * 1000; // توقف 10 دقائق = استراحة
constexpr uint32_t REST_REMINDER_MS     = 30UL * 60 * 1000;
constexpr bool     REST_REMIND_ONCE     = true;

// ================== النسخة والسجلات ==================
constexpr const char* FW_VERSION       = "1.0.0";
// 0 = بدون، 1 = أخطاء، 2 = + تحذيرات، 3 = + معلومات. يُغيّر أثناء التشغيل بأمر LOG n
constexpr uint8_t  LOG_LEVEL           = 2;   // النسخة النهائية: تحذيرات وأخطاء فقط
constexpr uint32_t SERIAL_BAUD         = 115200;

inline uint8_t logLevel = LOG_LEVEL;   // المستوى الحالي (قابل للتغيير)

}  // namespace cfg

// ================== سجلات بمستويات ==================
#define LOG_E(fmt, ...) do { if (cfg::logLevel >= 1) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_W(fmt, ...) do { if (cfg::logLevel >= 2) Serial.printf("[W] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_I(fmt, ...) do { if (cfg::logLevel >= 3) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__); } while (0)
