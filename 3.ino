
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;

bool ahtOK = false;
bool bmpOK = false;

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.println("\nStarting AHT20 + BMP280...");

  ahtOK = aht.begin(&Wire);

  // نجرب عنوان BMP280 الشائعين
  bmpOK = bmp.begin(0x77);

  if (!bmpOK) {
    bmpOK = bmp.begin(0x76);
  }

  Serial.print("AHT20: ");
  Serial.println(ahtOK ? "Detected" : "Not detected");

  Serial.print("BMP280: ");
  Serial.println(bmpOK ? "Detected" : "Not detected");
}

void loop() {
  if (ahtOK) {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);

    Serial.print("AHT20 Temperature: ");
    Serial.print(temp.temperature);
    Serial.println(" C");

    Serial.print("Humidity: ");
    Serial.print(humidity.relative_humidity);
    Serial.println(" %");
  }

  if (bmpOK) {
    Serial.print("BMP280 Temperature: ");
    Serial.print(bmp.readTemperature());
    Serial.println(" C");

    Serial.print("Pressure: ");
    Serial.print(bmp.readPressure() / 100.0F);
    Serial.println(" hPa");
  }

  Serial.println("----------------------");
  delay(2000);
}
