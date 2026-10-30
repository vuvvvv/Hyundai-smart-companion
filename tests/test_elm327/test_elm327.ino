#include <Arduino.h>
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth غير مفعّل في هذه اللوحة"
#endif
#if !defined(CONFIG_BT_SPP_ENABLED)
#error "Serial Port Profile غير متوفر"
#endif

BluetoothSerial SerialBT;

const char* OBD_NAME = "OBDII";
const char* OBD_PIN  = "1234";
uint8_t OBD_MAC[6]   = {0x00, 0x10, 0xCC, 0x4F, 0x36, 0x03};

volatile bool btConnected = false;
uint32_t lastTry = 0;
String inputLine;

// ================== أحداث البلوتوث (للتشخيص) ==================
void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t* p) {
  switch (event) {
    case ESP_SPP_INIT_EVT:
      Serial.println("[BT] SPP initialized");
      break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
      Serial.printf("[BT] SDP done: status=%d, channels=%d ->", p->disc_comp.status, p->disc_comp.scn_num);
      for (int i = 0; i < p->disc_comp.scn_num; i++) Serial.printf(" %d", p->disc_comp.scn[i]);
      Serial.println();
      break;
    case ESP_SPP_CL_INIT_EVT:
      Serial.printf("[BT] Client init: status=%d\n", p->cl_init.status);
      break;
    case ESP_SPP_OPEN_EVT:
      Serial.printf("[BT] OPEN: status=%d\n", p->open.status);
      btConnected = true;
      break;
    case ESP_SPP_CLOSE_EVT:
      Serial.printf("[BT] CLOSED: status=%d\n", p->close.status);
      btConnected = false;
      break;
    default:
      break;
  }
}

// ================== مسح الأجهزة ==================
bool scanForOBD() {
  Serial.println("\n[SCAN] Scanning 10s ...");
  BTScanResults* r = SerialBT.discover(10000);
  bool found = false;
  BTAddress target(OBD_MAC);

  if (r && r->getCount() > 0) {
    for (int i = 0; i < r->getCount(); i++) {
      BTAdvertisedDevice* d = r->getDevice(i);
      Serial.printf("  %s  name='%s'  rssi=%d\n",
                    d->getAddress().toString().c_str(),
                    d->getName().c_str(),
                    d->haveRSSI() ? d->getRSSI() : 0);
      if (d->getAddress().equals(target) || d->getName() == OBD_NAME) found = true;
    }
  } else {
    Serial.println("  No devices found");
  }

  Serial.println(found ? "[SCAN] OBDII FOUND" : "[SCAN] OBDII NOT visible (is the phone still connected?)");
  return found;
}

// ================== محاولات الاتصال ==================
bool tryConnect() {
  BTAddress addr(OBD_MAC);

  struct Attempt { int ch; esp_spp_sec_t sec; const char* desc; };
  Attempt tries[] = {
    {1, ESP_SPP_SEC_NONE,         "channel 1, no security"},
    {0, ESP_SPP_SEC_NONE,         "auto channel (SDP), no security"},
    {1, ESP_SPP_SEC_AUTHENTICATE, "channel 1, authenticate (PIN)"},
    {0, (esp_spp_sec_t)(ESP_SPP_SEC_ENCRYPT | ESP_SPP_SEC_AUTHENTICATE), "auto channel, encrypt+auth"},
  };

  for (auto& t : tries) {
    Serial.printf("\n[CONN] Trying MAC: %s ...\n", t.desc);
    if (SerialBT.connect(addr, t.ch, t.sec, ESP_SPP_ROLE_MASTER)) {
      Serial.printf("[CONN] SUCCESS with: %s\n", t.desc);
      return true;
    }
    Serial.println("[CONN] failed");
    SerialBT.disconnect();
    delay(1500);
  }

  Serial.println("\n[CONN] Trying by name 'OBDII' ...");
  if (SerialBT.connect(OBD_NAME)) {
    Serial.println("[CONN] SUCCESS by name");
    return true;
  }
  Serial.println("[CONN] All attempts failed");
  return false;
}

// ================== إرسال أمر للـ ELM327 وانتظار '>' ==================
String elmCmd(const String& cmd, uint32_t timeoutMs = 3000) {
  delay(150);                                            // راحة للـ ELM بين الأوامر
  while (SerialBT.available()) { SerialBT.read(); delay(2); }   // تنظيف البقايا

  String out = cmd + "\r";
  SerialBT.write((const uint8_t*)out.c_str(), out.length());    // حزمة واحدة

  String resp;
  bool gotPrompt = false;
  uint32_t start = millis();
  while (!gotPrompt && millis() - start < timeoutMs) {
    while (SerialBT.available()) {
      char c = SerialBT.read();
      if (c == '>') { gotPrompt = true; break; }
      if (c == '\r' || c == '\n') {
        if (resp.length() && !resp.endsWith(" | ")) resp += " | ";
      } else if (c != 0) {
        resp += c;
      }
    }
    delay(1);
  }

  resp.trim();
  // إزالة الـ echo لو كان شغال
  if (resp.startsWith(cmd)) {
    resp = resp.substring(cmd.length());
    resp.trim();
    if (resp.startsWith("|")) { resp = resp.substring(1); resp.trim(); }
  }
  if (resp.endsWith("|")) { resp.remove(resp.length() - 1); resp.trim(); }
  if (!gotPrompt) resp += " [TIMEOUT]";
  return resp;
}

// يكرر الأمر حتى يظهر الرد المتوقع
bool elmExpect(const String& cmd, const char* expect, int tries = 4, uint32_t timeoutMs = 2000) {
  for (int i = 0; i < tries; i++) {
    String r = elmCmd(cmd, timeoutMs);
    Serial.printf("  %-6s -> %s\n", cmd.c_str(), r.c_str());
    if (r.indexOf(expect) >= 0) return true;
    delay(300);
  }
  return false;
}

// ===== فك تشفير ردود PID =====
bool pidData(const String& resp, const String& pid, String& data) {
  String clean = resp;
  clean.replace(" ", "");
  String key = "41" + pid.substring(2);          // 010C -> 410C
  int p = clean.indexOf(key);
  if (p < 0) return false;
  int end = clean.indexOf('|', p);
  if (end < 0) end = clean.length();
  data = clean.substring(p + 4, end);
  return data.length() >= 2;
}

int hexAt(const String& s, int byteIndex) {
  return strtol(s.substring(byteIndex * 2, byteIndex * 2 + 2).c_str(), nullptr, 16);
}

void readLive() {
  String d;
  String r;

  r = elmCmd("010C", 2000);
  if (pidData(r, "010C", d) && d.length() >= 4)
    Serial.printf("RPM: %d   ", (hexAt(d, 0) * 256 + hexAt(d, 1)) / 4);
  else Serial.printf("RPM: [%s]   ", r.c_str());

  r = elmCmd("010D", 2000);
  if (pidData(r, "010D", d)) Serial.printf("Speed: %d km/h   ", hexAt(d, 0));
  else Serial.printf("Speed: [%s]   ", r.c_str());

  r = elmCmd("0105", 2000);
  if (pidData(r, "0105", d)) Serial.printf("Coolant: %d C", hexAt(d, 0) - 40);
    else Serial.printf("Coolant: [%s]", r.c_str());

  r = elmCmd("0146", 2000);
  if (pidData(r, "0146", d)) Serial.printf("   Ambient: %d C", hexAt(d, 0) - 40);
  else Serial.printf("   Ambient: [%s]", r.c_str());

  Serial.println();
}


void runCmd(const String& cmd, uint32_t timeoutMs = 3000) {
  String r = elmCmd(cmd, timeoutMs);
  Serial.printf("  %-6s -> %s\n", cmd.c_str(), r.c_str());
}

// ================== تهيئة ELM327 وقراءة تجريبية ==================
bool liveMode = false;

void elmInitAndTest() {
  Serial.println("\n===== ELM327 INIT =====");

  // Reset: القطع المقلدة بطيئة، ننتظر ثم ننظف
  elmCmd("ATZ", 5000);
  delay(1500);
  while (SerialBT.available()) SerialBT.read();

  elmExpect("ATE0", "OK");          // نتأكد أن الـ echo انطفأ فعلاً
  elmExpect("ATL0", "OK");
  elmExpect("ATS0", "OK");
  elmExpect("ATH0", "OK");
  elmExpect("ATAT1", "OK");         // Adaptive timing
  runCmd("ATI");
  runCmd("ATRV");
  elmExpect("ATSP0", "OK");

  Serial.println("\n===== PROTOCOL DETECT (ignition ON) =====");
  String r = elmCmd("0100", 15000);
  Serial.printf("  0100   -> %s\n", r.c_str());

  if (r.indexOf("4100") >= 0) {
    delay(300);
    String dp = elmCmd("ATDPN", 2000);
    Serial.printf("  ATDPN  -> %s\n", dp.c_str());
    // A6 -> نثبت البروتوكول 6 حتى لا يبحث كل مرة
    int i = dp.indexOf('A');
    if (i >= 0 && i + 1 < (int)dp.length()) {
      String proto = dp.substring(i + 1, i + 2);
      elmExpect("ATSP" + proto, "OK");
    }
    Serial.println("\n===== LIVE TEST =====");
    readLive();
    Serial.println("\nType LIVE for continuous reading, STOP to stop, or any OBD command.\n");
  } else {
    Serial.println("  Car not responding. Turn ignition ON and type 0100");
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 -> ELM327 ROBUST TEST ===");
  Serial.printf("Arduino-ESP32 core: %d.%d.%d\n",
                ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);

  SerialBT.register_callback(btCallback);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  SerialBT.disableSSP();   // استخدام PIN القديم (Legacy) بدل SSP
#endif

  if (!SerialBT.begin("ESP32_OBD", true)) {   // true = Master
    Serial.println("ERROR: Bluetooth init failed");
    while (true) delay(1000);
  }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  SerialBT.setPin(OBD_PIN, strlen(OBD_PIN));
#else
  SerialBT.setPin(OBD_PIN);
#endif
  Serial.println("Bluetooth started (master), PIN set.");

  scanForOBD();

  btConnected = tryConnect();
  if (btConnected) {
    delay(500);
    elmInitAndTest();
  }
  lastTry = millis();
}

// ================== LOOP ==================
void loop() {
  // إعادة المحاولة كل 15 ثانية
  if (!SerialBT.connected()) {
    if (millis() - lastTry > 15000) {
      lastTry = millis();
      Serial.println("\n[RETRY] Reconnecting...");
      if (tryConnect()) { delay(500); elmInitAndTest(); }
    }
    return;
  }

  // أوامر يدوية من Serial Monitor (سطر كامل)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      inputLine.trim();
            if (inputLine.length()) {
        inputLine.toUpperCase();
        if (inputLine == "LIVE")      { liveMode = true;  Serial.println("[LIVE ON]"); }
        else if (inputLine == "STOP") { liveMode = false; Serial.println("[LIVE OFF]"); }
        else runCmd(inputLine, 8000);
        inputLine = "";
      }
    } else {
      inputLine += c;
    }
  }

  // أي بيانات تأتي بدون طلب
    // Keep-alive: نبقي القطعة مشغولة حتى لا تقطع الاتصال
  static uint32_t lastKeepAlive = 0;
  if (!liveMode && millis() - lastKeepAlive > 3000) {
    lastKeepAlive = millis();
    elmCmd("ATRV", 1000);
  }

  // القراءة المستمرة عند كتابة LIVE
  static uint32_t lastLive = 0;
  if (liveMode && millis() - lastLive > 1000) {
    lastLive = millis();
    readLive();
  }

  // أي بيانات تأتي بدون طلب
  while (SerialBT.available()) Serial.write(SerialBT.read());
  delay(2);
}
