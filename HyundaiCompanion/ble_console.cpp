// ===== ble_console.cpp =====
#include "ble_console.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>
#include <BLE2902.h>
#include "config.h"

namespace ble_console {
namespace {

// Nordic UART Service: الجوال يكتب في RX، والجهاز يرسل من TX
constexpr const char* UART_SVC = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* UART_RX  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* UART_TX  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

BLECharacteristic* txChar = nullptr;
volatile bool isConnected = false;

// ================== الوارد (يُكتب من مهمة البلوتوث) ==================
// السطر يكتمل بنهاية سطر، أو بتوقف الكتابة 300ms (تطبيقات ترسل بلا نهاية سطر)
constexpr size_t RX_LINE_MAX = 64;
constexpr uint32_t LINE_IDLE_MS = 300;
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
char     rxBuf[RX_LINE_MAX];
uint8_t  rxLen = 0;
uint32_t rxLastMs = 0;
char     lineBuf[RX_LINE_MAX];
bool     lineReady = false;

void finishLine() {   // داخل القفل
  if (rxLen && !lineReady) {
    memcpy(lineBuf, rxBuf, rxLen);
    lineBuf[rxLen] = 0;
    lineReady = true;
  }
  rxLen = 0;
}

// ================== الصادر (من الحلقة الرئيسية فقط) ==================
constexpr size_t TX_SIZE = 2048;
constexpr size_t CHUNK = 20;              // حمولة الإشعار مع MTU الافتراضي
char     txRing[TX_SIZE];
size_t   txHead = 0, txTail = 0;
uint32_t lastTxMs = 0;

void pushTx(uint8_t c) {
  if (!isConnected) return;
  size_t next = (txHead + 1) % TX_SIZE;
  if (next == txTail) return;             // ممتلئ: يُهمل الباقي
  txRing[txHead] = c;
  txHead = next;
}

// كل ما يُطبع للرد يذهب إلى Serial والجوال معاً
class TeePrint : public Print {
public:
  size_t write(uint8_t c) override { Serial.write(c); pushTx(c); return 1; }
  size_t write(const uint8_t* b, size_t n) override {
    Serial.write(b, n);
    for (size_t i = 0; i < n; i++) pushTx(b[i]);
    return n;
  }
};
TeePrint tee;

// ================== الاستدعاءات (مهمة البلوتوث) ==================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { isConnected = true; }
  void onDisconnect(BLEServer*) override { isConnected = false; }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    portENTER_CRITICAL(&rxMux);
    for (size_t i = 0; i < v.length(); i++) {
      char ch = v[i];
      if (ch == '\r' || ch == '\n') finishLine();
      else if (rxLen < RX_LINE_MAX - 1) rxBuf[rxLen++] = ch;
    }
    rxLastMs = millis();
    portEXIT_CRITICAL(&rxMux);
  }
};

}  // namespace

// ================== الواجهة العامة ==================
void begin() {
  if (!cfg::BLE_ENABLED) return;

  BLEDevice::init(cfg::BT_DEVICE_NAME);

  // اقتران بالرمز الثابت مع حماية MITM وتشفير، والجهاز يتذكر الجوال بعد أول اقتران
  BLESecurity* sec = new BLESecurity();
  sec->setPassKey(true, cfg::BLE_PASSKEY);
  sec->setCapability(ESP_IO_CAP_OUT);
  sec->setAuthenticationMode(true, true, true);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  server->advertiseOnDisconnect(true);

  BLEService* svc = server->createService(UART_SVC);
  txChar = svc->createCharacteristic(UART_TX, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());
  txChar->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);

  BLECharacteristic* rx = svc->createCharacteristic(
      UART_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);   // لا أوامر بلا رمز
  rx->setCallbacks(new RxCallbacks());
  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(UART_SVC);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);   // يساعد اتصال iPhone
  adv->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  LOG_I("BLE console ready as %s", cfg::BT_DEVICE_NAME);   // لا يُطبع الرمز
}

void loop() {
  if (!txChar) return;
  if (!isConnected) { txTail = txHead; return; }
  if (txTail == txHead || millis() - lastTxMs < cfg::BLE_TX_CHUNK_MS) return;

  uint8_t chunk[CHUNK];
  size_t n = 0;
  while (n < CHUNK && txTail != txHead) {
    chunk[n++] = txRing[txTail];
    txTail = (txTail + 1) % TX_SIZE;
  }
  txChar->setValue(chunk, n);
  txChar->notify();
  lastTxMs = millis();
}

bool takeLine(char* out, size_t n) {
  bool got = false;
  portENTER_CRITICAL(&rxMux);
  if (!lineReady && rxLen && millis() - rxLastMs > LINE_IDLE_MS) finishLine();
  if (lineReady) {
    strlcpy(out, lineBuf, n);
    lineReady = false;
    got = true;
  }
  portEXIT_CRITICAL(&rxMux);
  return got;
}

bool connected() { return isConnected; }
Print& out() { return tee; }

}  // namespace ble_console
