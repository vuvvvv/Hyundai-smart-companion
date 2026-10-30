
#define VIBRATION_PIN 27

const unsigned long WINDOW_MS = 250;
const int STRONG_THRESHOLD = 24;

int lastValue;
int changes = 0;
unsigned long windowStart = 0;

// تهيئة الحساس
void setup() {
  Serial.begin(115200);
  pinMode(VIBRATION_PIN, INPUT);

  lastValue = digitalRead(VIBRATION_PIN);
  windowStart = millis();
}

// ترجع true عند اكتشاف اهتزاز قوي
bool strongVibrationDetected() {
  unsigned long now = millis();
  int value = digitalRead(VIBRATION_PIN);

  if (value != lastValue) {
    changes++;
    lastValue = value;
  }

  if (now - windowStart >= WINDOW_MS) {
    bool strong = (changes >= STRONG_THRESHOLD);

    changes = 0;
    windowStart = now;

    return strong;
  }

  return false;
}

void loop() {
  if (strongVibrationDetected()) {
    Serial.println("STRONG VIBRATION!");
  }
}
