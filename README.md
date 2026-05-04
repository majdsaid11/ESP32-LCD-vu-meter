# DJ Dual Meter - ESP32 Audio Visualizer

مشروع باستخدام ESP32 لعرض مستوى الصوت على شاشة TFT ST7789، مع مايكروفون INMP441 و RGB LED يتفاعل مع ضربات الـ Bass.

يعرض المشروع عدادين:
- Bass Meter
- Voice / Treble Meter

مع تأثير Peak Line وحركة ناعمة مثل عدادات DJ.

---

## المكونات المطلوبة

- ESP32
- شاشة ST7789 TFT SPI مقاس 240x320
- مايكروفون INMP441 I2S
- RGB LED
- مقاومات 220Ω لليد
- أسلاك توصيل

---

## المكتبات المطلوبة

ثبّت المكتبات التالية من Arduino IDE Library Manager:

- Adafruit GFX Library
- Adafruit ST7789 and ST7735 Library
- SPI

مكتبة I2S تأتي مع ESP32 Core.

---

## توصيلات شاشة ST7789

| TFT Pin | ESP32 Pin |
|---|---|
| VCC | 3.3V أو 5V حسب نوع الشاشة |
| GND | GND |
| CS | GPIO14 |
| DC | GPIO21 |
| RST | GPIO22 |
| MOSI | GPIO23 |
| SCLK | GPIO18 |
| BL | GPIO32 |

---

## توصيلات مايكروفون INMP441

| INMP441 Pin | ESP32 Pin |
|---|---|
| VDD | 3.3V |
| GND | GND |
| WS | GPIO25 |
| SCK | GPIO26 |
| SD | GPIO27 |
| L/R | GND للـ Left Channel |

ملاحظة: إذا وصلت L/R على 3.3V، غيّر إعداد القناة في الكود إلى Right Channel.

---

## توصيلات RGB LED

| RGB LED | ESP32 Pin |
|---|---|
| Red | GPIO13 |
| Green | GPIO33 |
| Blue | GPIO12 |

استخدم مقاومة 220Ω تقريباً مع كل لون.

إذا كان الليد Common Cathode:
```cpp
const bool RGB_COMMON_ANODE = false;
