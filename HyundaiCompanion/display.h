// ===== display.h — الواجهات الثلاث، شريط الحالة، الصور النقطية =====
// المرحلة 2: الواجهة العادية + شريط الحالة. القيادة والتنبيه في المرحلة 6.
#pragma once
#include <Arduino.h>

namespace display {

void begin();
void loop();     // يرسم حسب معدل التحديث، ويفحص وجود الشاشة ويعيد تهيئتها

}  // namespace display
