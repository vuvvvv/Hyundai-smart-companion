#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

HardwareSerial dfSerial(2);
DFRobotDFPlayerMini player;

#define DF_RX 16
#define DF_TX 17

void setup() {
  Serial.begin(115200);
  dfSerial.begin(9600, SERIAL_8N1, DF_RX, DF_TX);

  delay(2000);

  if (!player.begin(dfSerial, false, true)) {
    Serial.println("Connection failed");
    return;
  }

  player.volume(100);
  delay(1000);

  Serial.println("Playing track 1");
  player.playMp3Folder(1);
}

void loop() {
  if (player.available()) {
    uint8_t type = player.readType();
    int value = player.read();

    Serial.print("Type: ");
    Serial.print(type);
    Serial.print(" Value: ");
    Serial.println(value);
  }
}