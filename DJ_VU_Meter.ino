#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <driver/i2s.h>
#include <math.h>

// بعض إصدارات ESP32 Core بتستخدم اسم مختلف للـ I2S standard format
// هذا الشرط بيخلي الكود يشتغل مع الإصدارات القديمة والجديدة.
#ifndef I2S_COMM_FORMAT_STAND_I2S
#define I2S_COMM_FORMAT_STAND_I2S I2S_COMM_FORMAT_I2S
#endif

// =====================================================
// PROJECT WIRING / توصيلات المشروع
// =====================================================
// Board: ESP32
// Display: ST7789 TFT SPI 240x320
// Microphone: INMP441 I2S
// LED: RGB LED على PWM pins
//
// TFT ST7789 Wiring:
// TFT VCC  -> 3.3V أو 5V حسب نوع الشاشة
// TFT GND  -> GND
// TFT CS   -> GPIO14
// TFT DC   -> GPIO21
// TFT RST  -> GPIO22
// TFT MOSI -> GPIO23
// TFT SCLK -> GPIO18
// TFT BL   -> GPIO32
//
// INMP441 Microphone Wiring:
// INMP441 VDD -> 3.3V
// INMP441 GND -> GND
// INMP441 WS  -> GPIO25
// INMP441 SCK -> GPIO26
// INMP441 SD  -> GPIO27
// INMP441 L/R -> GND إذا بدك LEFT channel
// INMP441 L/R -> 3.3V إذا بدك RIGHT channel
//
// RGB LED Wiring:
// Red   -> GPIO13 مع مقاومة 220Ω تقريباً
// Green -> GPIO33 مع مقاومة 220Ω تقريباً
// Blue  -> GPIO12 مع مقاومة 220Ω تقريباً
// الطرف المشترك:
// Common Cathode -> GND و RGB_COMMON_ANODE = false
// Common Anode   -> 3.3V و RGB_COMMON_ANODE = true
//
// ملاحظة مهمة: GPIO34 و GPIO35 على ESP32 input فقط، لذلك لا ينفعوا للـ LED.

// =====================================================
// TFT PINS / أرجل الشاشة
// =====================================================
#define TFT_CS    14
#define TFT_DC    21
#define TFT_RST   22
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_BL    32

// =====================================================
// MIC PINS - INMP441 / أرجل المايكروفون
// =====================================================
#define I2S_WS    25
#define I2S_SCK   26
#define I2S_SD    27
#define I2S_PORT  I2S_NUM_0

// =====================================================
// RGB LED PINS / أرجل الليد
// =====================================================
// D13 -> Red
// D33 -> Green
// D12 -> Blue
// لا تستخدم D34 / D35 للـ LED لأنهم Input فقط
#define RGB_R_PIN 13
#define RGB_G_PIN 33
#define RGB_B_PIN 12

// تعريف كائن الشاشة من مكتبة Adafruit_ST7789
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// =====================================================
// USER SETTINGS / إعدادات عامة
// =====================================================

// إذا L/R على GND خليه LEFT
// إذا L/R على VDD بدّله RIGHT
#define MIC_CHANNEL_FORMAT I2S_CHANNEL_FMT_ONLY_LEFT
// #define MIC_CHANNEL_FORMAT I2S_CHANNEL_FMT_ONLY_RIGHT

// معدل أخذ العينات من المايكروفون بالهرتز
const int SAMPLE_RATE   = 16000;

// عدد العينات التي يتم قراءتها بكل دورة من I2S
const int SAMPLE_COUNT  = 256;

// تخفيض قيمة عينة المايك لأن INMP441 يعطي 32-bit data
const int MIC_BIT_SHIFT = 10;

// الشاشة هدفها 60FPS
const int TARGET_FPS = 60;
const uint32_t FRAME_INTERVAL_US = 1000000UL / TARGET_FPS;

// =====================================================
// BASS SETTINGS / إعدادات البيس
// =====================================================
// القيم التالية تتحكم بحساسية عداد Bass وطريقة ضغط الإشارة.
const float RMS_NOISE_GATE       = 2200.0f;
const float SILENCE_OFFSET       = 0.22f;
const int   MIN_BAR_CUTOFF       = 18;

// Hysteresis حتى لا يهتز العداد عند الهدوء
const int   SILENCE_ENTER        = 18;
const int   SILENCE_EXIT         = 28;

// تطبيع وضغط RMS حتى لا يصل العداد للنهاية بسرعة
const float RMS_NORMALIZE_TOP    = 32000.0f;
const float RMS_COMPRESS_START   = 5000.0f;
const float RMS_COMPRESS_RATIO   = 0.22f;

// =====================================================
// TREBLE / VOICE SETTINGS / إعدادات الصوت العالي والتريبل
// =====================================================
// Voice band تقريباً بين 650Hz و 4200Hz
const float VOICE_LOW_CUT_HZ     = 650.0f;
const float VOICE_HIGH_CUT_HZ    = 4200.0f;
const float TREBLE_CUT_HZ        = 2800.0f;

// وزن الصوت والتريبل في حساب مستوى العداد
const float VOICE_WEIGHT         = 0.75f;
const float TREBLE_WEIGHT        = 0.30f;

// بوابة وحساسية عداد Voice/Treble
const float VOICE_GATE           = 650.0f;
const float VOICE_FULL_SCALE     = 17000.0f;
const float VOICE_CURVE          = 1.80f;

const int   VOICE_MIN_PIXEL      = 15;

// =====================================================
// MOVEMENT SETTINGS / إعدادات حركة العدادات
// =====================================================
// Attack: سرعة ارتفاع العداد
// Release: سرعة نزول العداد حسب المستوى
const float ATTACK_SPEED         = 0.92f;
const float RELEASE_SPEED_LOW    = 0.18f;
const float RELEASE_SPEED_MID    = 0.14f;
const float RELEASE_SPEED_HIGH   = 0.10f;

// Peak line: الخط الأبيض الذي يبقى لحظة عند أعلى مستوى
const bool  ENABLE_PEAK_LINE     = true;
const float PEAK_DROP_LOW        = 4.0f;
const float PEAK_DROP_MID        = 6.5f;
const float PEAK_DROP_HIGH       = 10.0f;
const int   PEAK_HOLD_MS         = 50;
const int   PEAK_FALL_MS         = 12;

// فعّلها true إذا بدك تشوف قيم RMS على Serial Monitor
const bool  DEBUG_SERIAL_LEVELS  = false;

// =====================================================
// SMART RGB LED SETTINGS - MEDIUM + LIGHTER BASS HOLD
// إعدادات الليد الذكي مع ضربات البيس
// =====================================================
const bool  RGB_ENABLE           = true;

// إذا الليد RGB عندك Common Cathode خليه false
// إذا Common Anode خليه true
const bool  RGB_COMMON_ANODE     = false;

// حساسية ضربة البيس للـ RGB فقط
const int   RGB_MIN_BASS_HIT     = 60;
const float RGB_HIT_MARGIN       = 22.0f;
const float RGB_AVG_SPEED        = 0.055f;
const float RGB_REARM_RATIO      = 0.62f;

// وسط بين السريع والناعم
const int   RGB_HIT_COOLDOWN_MS  = 130;

// تثبيت أخف من النسخة السابقة
// اللون يتغير أسرع لما يصير في فراغ بسيط بالـ Bass
const int   RGB_QUIET_BEFORE_COLOR_CHANGE_MS = 110;
const float RGB_QUIET_LEVEL_RATIO            = 0.58f;

// Fade وسط
const float RGB_FLASH_DECAY      = 0.89f;
const float RGB_BASS_GLOW        = 0.28f;
const int   RGB_PWM_MAX          = 255;

// تنعيم متوسط
const float RGB_SMOOTH_SPEED     = 0.14f;

// لمعة بسيطة مع التريبل
const bool  RGB_TREBLE_SPARKLE   = true;

// =====================================================
// COLORS / ألوان واجهة الشاشة
// =====================================================
#define COLOR_BG        ST77XX_BLACK
#define COLOR_FRAME     ST77XX_WHITE
#define COLOR_TEXT      ST77XX_WHITE
#define COLOR_SUBTEXT   ST77XX_CYAN
#define COLOR_LOW       ST77XX_GREEN
#define COLOR_MID       ST77XX_YELLOW
#define COLOR_HIGH      ST77XX_RED
#define COLOR_PEAK      ST77XX_WHITE
#define COLOR_GRID      0x39E7
#define COLOR_PANEL     0x0841
#define COLOR_OFFSEG    0x1082

// =====================================================
// SCREEN LAYOUT / أماكن العناصر على الشاشة
// =====================================================
const int SCREEN_W = 320;
const int SCREEN_H = 240;

const int TITLE_Y   = 14;

const int PANEL_X   = 18;
const int PANEL_Y   = 44;
const int PANEL_W   = 284;
const int PANEL_H   = 152;

const int BAR_X     = 34;
const int BAR_W     = 252;
const int BAR_H     = 24;

const int BAR_Y_VOICE = 78;
const int BAR_Y_BASS  = 132;

const int SEGMENTS  = 42;
const int SEG_GAP   = 1;

const int PERCENT_Y = 206;

// =====================================================
// BIQUAD FILTER / فلتر رقمي للصوت
// =====================================================
// هذا الفلتر يستخدم لفصل نطاقات الصوت:
// High-pass لإزالة الترددات المنخفضة، و Low-pass لتحديد نهاية النطاق.
struct Biquad {
  float b0, b1, b2;
  float a1, a2;
  float z1, z2;

  void reset() {
    z1 = 0.0f;
    z2 = 0.0f;
  }

  float process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }

  void setLowPass(float sampleRate, float freq, float q) {
    float w0 = 2.0f * PI * freq / sampleRate;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * q);

    float bb0 = (1.0f - cosw0) * 0.5f;
    float bb1 = 1.0f - cosw0;
    float bb2 = (1.0f - cosw0) * 0.5f;
    float aa0 = 1.0f + alpha;
    float aa1 = -2.0f * cosw0;
    float aa2 = 1.0f - alpha;

    b0 = bb0 / aa0;
    b1 = bb1 / aa0;
    b2 = bb2 / aa0;
    a1 = aa1 / aa0;
    a2 = aa2 / aa0;
    reset();
  }

  void setHighPass(float sampleRate, float freq, float q) {
    float w0 = 2.0f * PI * freq / sampleRate;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * q);

    float bb0 = (1.0f + cosw0) * 0.5f;
    float bb1 = -(1.0f + cosw0);
    float bb2 = (1.0f + cosw0) * 0.5f;
    float aa0 = 1.0f + alpha;
    float aa1 = -2.0f * cosw0;
    float aa2 = 1.0f - alpha;

    b0 = bb0 / aa0;
    b1 = bb1 / aa0;
    b2 = bb2 / aa0;
    a1 = aa1 / aa0;
    a2 = aa2 / aa0;
    reset();
  }
};

// =====================================================
// AUDIO STATE / متغيرات حالة الصوت والعدادات
// =====================================================
int32_t i2sSamples[SAMPLE_COUNT];

Biquad voiceHP1;
Biquad voiceHP2;
Biquad voiceLP1;
Biquad voiceLP2;

Biquad trebleHP1;
Biquad trebleHP2;

float bassDisplayLevel  = 0.0f;
float voiceDisplayLevel = 0.0f;

float bassPeakLevel     = 0.0f;
float voicePeakLevel    = 0.0f;

bool bassSilenceLatch   = true;

unsigned long bassLastPeakHold  = 0;
unsigned long bassLastPeakFall  = 0;
unsigned long voiceLastPeakHold = 0;
unsigned long voiceLastPeakFall = 0;

uint32_t lastDrawUs = 0;

// =====================================================
// RGB LED STATE / متغيرات حالة الليد
// =====================================================
struct RgbColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// أحمر، أزرق، أحمر، أزرق...
const RgbColor RGB_PALETTE[] = {
  {255, 0, 0},
  {0, 70, 255}
};

const int RGB_PALETTE_COUNT = sizeof(RGB_PALETTE) / sizeof(RGB_PALETTE[0]);

int rgbPaletteIndex = -1;
RgbColor rgbCurrentColor = {0, 0, 0};

float rgbBeatAverage = 0.0f;
float rgbFlashLevel  = 0.0f;
bool rgbBassArmed    = true;
unsigned long rgbLastHitMs = 0;

// مراقبة هدوء الـ Bass قبل تغيير اللون
unsigned long rgbQuietStartMs = 0;
bool rgbColorChangeReady = true;

// بنخزن مستويات البيس والتريبل بالبكسل حتى نرسمها على الشاشة
struct AudioLevels {
  int bassPx;
  int voicePx;
};

// =====================================================
// HELPERS / دوال مساعدة
// =====================================================
uint16_t segmentColorByIndex(int i) {
  float p = (float)i / (float)SEGMENTS;

  if (p < 0.60f) return COLOR_LOW;
  if (p < 0.80f) return COLOR_MID;
  return COLOR_HIGH;
}

int levelToSegments(int levelPx) {
  int seg = map(levelPx, 0, BAR_W, 0, SEGMENTS);
  return constrain(seg, 0, SEGMENTS);
}

int levelToPercent(int levelPx) {
  int pct = map(levelPx, 0, BAR_W, 0, 100);
  return constrain(pct, 0, 100);
}

// =====================================================
// BASS MAPPING / تحويل قوة البيس إلى طول العداد
// =====================================================
int bassOriginalRmsToLevel(float rms) {
  // تجاهل الضجيج الضعيف جداً
  if (rms < RMS_NOISE_GATE) return 0;

  // ضغط الإشارة العالية حتى لا يمتلئ العداد بسرعة
  if (rms > RMS_COMPRESS_START) {
    rms = RMS_COMPRESS_START + (rms - RMS_COMPRESS_START) * RMS_COMPRESS_RATIO;
  }

  float normalized = rms / RMS_NORMALIZE_TOP;
  normalized = constrain(normalized, 0.0f, 1.0f);

  normalized = (normalized - SILENCE_OFFSET) / (1.0f - SILENCE_OFFSET);
  normalized = constrain(normalized, 0.0f, 1.0f);

  int levelPx = (int)(normalized * BAR_W);

  if (levelPx < MIN_BAR_CUTOFF) {
    levelPx = 0;
  }

  // Silence latch يمنع التذبذب بين 0 وقيمة صغيرة عند الهدوء
  if (bassSilenceLatch) {
    if (levelPx <= SILENCE_EXIT) {
      levelPx = 0;
    } else {
      bassSilenceLatch = false;
    }
  } else {
    if (levelPx <= SILENCE_ENTER) {
      levelPx = 0;
      bassSilenceLatch = true;
    }
  }

  return constrain(levelPx, 0, BAR_W);
}

// =====================================================
// TREBLE MAPPING / تحويل قوة التريبل إلى طول العداد
// =====================================================
int voiceTrebleToLevel(float amp) {
  if (amp < VOICE_GATE) {
    return 0;
  }

  float normalized = (amp - VOICE_GATE) / (VOICE_FULL_SCALE - VOICE_GATE);
  normalized = constrain(normalized, 0.0f, 1.0f);

  // Curve يجعل العداد أقل حساسية للإشارات الصغيرة وأكثر طبيعية بصرياً
  normalized = powf(normalized, VOICE_CURVE);

  int levelPx = (int)(normalized * BAR_W);

  if (levelPx < VOICE_MIN_PIXEL) {
    levelPx = 0;
  }

  return constrain(levelPx, 0, BAR_W);
}

// تنعيم حركة العداد: صعود سريع ونزول أهدأ
void smoothMeter(int rawLevel, float &displayLevel) {
  if (rawLevel > displayLevel) {
    displayLevel = displayLevel + (rawLevel - displayLevel) * ATTACK_SPEED;
  } else {
    float releaseSpeed;

    if (displayLevel > BAR_W * 0.80f) {
      releaseSpeed = RELEASE_SPEED_HIGH;
    } else if (displayLevel > BAR_W * 0.45f) {
      releaseSpeed = RELEASE_SPEED_MID;
    } else {
      releaseSpeed = RELEASE_SPEED_LOW;
    }

    displayLevel = displayLevel + (rawLevel - displayLevel) * releaseSpeed;
  }

  if (rawLevel == 0 && displayLevel < 7.0f) {
    displayLevel = 0.0f;
  }

  if (displayLevel < 0.5f) {
    displayLevel = 0.0f;
  }
}

// تحديث خط الـ Peak الأبيض
void updatePeak(float displayLevel,
                float &peakLevel,
                unsigned long &lastPeakHold,
                unsigned long &lastPeakFall) {
  if (!ENABLE_PEAK_LINE) {
    peakLevel = displayLevel;
    return;
  }

  if (displayLevel <= 0.0f && peakLevel < 8.0f) {
    peakLevel = 0.0f;
  }

  if (displayLevel > peakLevel) {
    peakLevel = displayLevel;
    lastPeakHold = millis();
    lastPeakFall = millis();
  } else {
    if (millis() - lastPeakHold >= PEAK_HOLD_MS) {
      if (millis() - lastPeakFall >= PEAK_FALL_MS) {
        float drop;

        if (peakLevel > BAR_W * 0.80f) {
          drop = PEAK_DROP_HIGH;
        } else if (peakLevel > BAR_W * 0.50f) {
          drop = PEAK_DROP_MID;
        } else {
          drop = PEAK_DROP_LOW;
        }

        peakLevel -= drop;

        if (peakLevel < displayLevel) peakLevel = displayLevel;
        if (peakLevel < 0.0f) peakLevel = 0.0f;

        lastPeakFall = millis();
      }
    }
  }
}

// =====================================================
// SMART RGB LED / التحكم الذكي بالليد
// =====================================================
void writeRgbLed(uint8_t r, uint8_t g, uint8_t b) {
  if (!RGB_ENABLE) return;

  // إذا الليد Common Anode لازم نعكس قيم PWM
  if (RGB_COMMON_ANODE) {
    r = RGB_PWM_MAX - r;
    g = RGB_PWM_MAX - g;
    b = RGB_PWM_MAX - b;
  }

  analogWrite(RGB_R_PIN, r);
  analogWrite(RGB_G_PIN, g);
  analogWrite(RGB_B_PIN, b);
}

// Gamma بسيط حتى تبدو الإضاءة أنعم للعين
uint8_t applyRgbGamma(float value) {
  value = constrain(value, 0.0f, 255.0f);

  int v = (int)(value + 0.5f);
  return (uint8_t)((v * v) / 255);
}

void setupRgbLed() {
  if (!RGB_ENABLE) return;

  pinMode(RGB_R_PIN, OUTPUT);
  pinMode(RGB_G_PIN, OUTPUT);
  pinMode(RGB_B_PIN, OUTPUT);

  writeRgbLed(0, 0, 0);
}

void nextRgbBassColor() {
  rgbPaletteIndex++;
  if (rgbPaletteIndex >= RGB_PALETTE_COUNT) rgbPaletteIndex = 0;

  rgbCurrentColor = RGB_PALETTE[rgbPaletteIndex];
}

void updateSmartRgbLed(int bassPx, int voicePx) {
  if (!RGB_ENABLE) return;

  static float smoothR = 0.0f;
  static float smoothG = 0.0f;
  static float smoothB = 0.0f;

  unsigned long now = millis();

  float bass = constrain((float)bassPx, 0.0f, (float)BAR_W);
  float voice = constrain((float)voicePx, 0.0f, (float)BAR_W);

  // Threshold متحرك حتى الليد يتأقلم مع صوت الغرفة والأغنية
  rgbBeatAverage = rgbBeatAverage + (bass - rgbBeatAverage) * RGB_AVG_SPEED;

  float hitTrigger = rgbBeatAverage + RGB_HIT_MARGIN;
  if (hitTrigger < RGB_MIN_BASS_HIT) hitTrigger = RGB_MIN_BASS_HIT;

  float rearmLevel = hitTrigger * RGB_REARM_RATIO;
  if (rearmLevel < 10.0f) rearmLevel = 10.0f;

  // مستوى الهدوء الحقيقي للـ Bass
  float quietLevel = hitTrigger * RGB_QUIET_LEVEL_RATIO;
  if (quietLevel < 18.0f) quietLevel = 18.0f;

  // اللون ما يتغير إلا إذا البيس نزل وظل نازل شوي
  // بهذه النسخة صار الشرط أخف حتى ما يثبت زيادة
  if (bass < quietLevel) {
    if (rgbQuietStartMs == 0) {
      rgbQuietStartMs = now;
    }

    if ((now - rgbQuietStartMs) >= RGB_QUIET_BEFORE_COLOR_CHANGE_MS) {
      rgbColorChangeReady = true;
    }
  } else {
    rgbQuietStartMs = 0;
  }

  // يسمح بنبضة سطوع جديدة
  if (bass < rearmLevel) {
    rgbBassArmed = true;
  }

  bool bassHit = (
    rgbBassArmed &&
    bass >= hitTrigger &&
    (now - rgbLastHitMs) >= RGB_HIT_COOLDOWN_MS
  );

  if (bassHit) {
    // غير اللون فقط بعد هدوء Bass واضح
    // أما Bass ممتد "دووووم" يثبت نفس اللون
    if (rgbColorChangeReady) {
      nextRgbBassColor();
      rgbColorChangeReady = false;
    }

    rgbFlashLevel = 1.0f;
    rgbBassArmed = false;
    rgbLastHitMs = now;
  }

  // Fade بدون delay
  rgbFlashLevel *= RGB_FLASH_DECAY;
  if (rgbFlashLevel < 0.01f) rgbFlashLevel = 0.0f;

  float bassNorm = bass / (float)BAR_W;
  float voiceNorm = voice / (float)BAR_W;

  float brightness = rgbFlashLevel;

  // Glow خفيف مع البيس بين الضربات
  float bassGlow = bassNorm * RGB_BASS_GLOW;
  if (bassGlow > brightness) brightness = bassGlow;

  brightness = constrain(brightness, 0.0f, 1.0f);

  float r = rgbCurrentColor.r * brightness;
  float g = rgbCurrentColor.g * brightness;
  float b = rgbCurrentColor.b * brightness;

  // لمعة بيضا خفيفة مع التريبل العالي
  if (RGB_TREBLE_SPARKLE && voiceNorm > 0.62f) {
    float sparkle = (voiceNorm - 0.62f) / 0.38f;
    sparkle = constrain(sparkle, 0.0f, 1.0f) * 55.0f;

    r += sparkle;
    g += sparkle;
    b += sparkle;
  }

  // تنعيم متوسط
  smoothR = smoothR + (r - smoothR) * RGB_SMOOTH_SPEED;
  smoothG = smoothG + (g - smoothG) * RGB_SMOOTH_SPEED;
  smoothB = smoothB + (b - smoothB) * RGB_SMOOTH_SPEED;

  writeRgbLed(
    applyRgbGamma(smoothR),
    applyRgbGamma(smoothG),
    applyRgbGamma(smoothB)
  );
}

// =====================================================
// UI / رسم واجهة الشاشة
// =====================================================
void drawMeterBack(int barY) {
  tft.drawRoundRect(BAR_X - 6, barY - 6, BAR_W + 12, BAR_H + 12, 8, COLOR_FRAME);

  for (int i = 1; i < 10; i++) {
    int x = BAR_X + (BAR_W * i) / 10;
    tft.drawFastVLine(x, barY - 2, BAR_H + 4, COLOR_GRID);
  }

  int segW = (BAR_W - (SEGMENTS - 1) * SEG_GAP) / SEGMENTS;

  for (int i = 0; i < SEGMENTS; i++) {
    int x = BAR_X + i * (segW + SEG_GAP);
    tft.fillRoundRect(x, barY, segW, BAR_H, 2, COLOR_OFFSEG);
  }
}

void drawStaticUI() {
  tft.fillScreen(COLOR_BG);

  tft.setTextWrap(false);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(84, TITLE_Y);
  tft.print("DJ Dual Meter");

  tft.fillRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 10, COLOR_PANEL);
  tft.drawRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 10, COLOR_FRAME);

  tft.setTextSize(1);

  tft.setTextColor(COLOR_SUBTEXT);
  tft.setCursor(BAR_X, BAR_Y_VOICE - 13);
  tft.print("VOICE / TREBLE");

  tft.setTextColor(COLOR_LOW);
  tft.setCursor(BAR_X, BAR_Y_BASS - 13);
  tft.print("BASS");

  drawMeterBack(BAR_Y_VOICE);
  drawMeterBack(BAR_Y_BASS);

  tft.setTextSize(1);

  tft.setTextColor(COLOR_LOW);
  tft.setCursor(BAR_X, BAR_Y_BASS + BAR_H + 8);
  tft.print("LOW");

  tft.setTextColor(COLOR_MID);
  tft.setCursor(BAR_X + BAR_W / 2 - 10, BAR_Y_BASS + BAR_H + 8);
  tft.print("MID");

  tft.setTextColor(COLOR_HIGH);
  tft.setCursor(BAR_X + BAR_W - 24, BAR_Y_BASS + BAR_H + 8);
  tft.print("HIGH");
}

void redrawSegmentAtPixel(int barY, int px, int currentSegs) {
  int segW = (BAR_W - (SEGMENTS - 1) * SEG_GAP) / SEGMENTS;

  for (int i = 0; i < SEGMENTS; i++) {
    int sx = i * (segW + SEG_GAP);

    if (px >= sx && px < sx + segW) {
      uint16_t c = (i < currentSegs) ? segmentColorByIndex(i) : COLOR_OFFSEG;
      tft.fillRoundRect(BAR_X + sx, barY, segW, BAR_H, 2, c);
      return;
    }
  }
}

void drawMeter(int meterIndex, int barY, int levelPx, int peakPx) {
  static int lastSegs[2]   = { -1, -1 };
  static int lastPeakPx[2] = { -1, -1 };

  levelPx = constrain(levelPx, 0, BAR_W);
  peakPx  = constrain(peakPx, 0, BAR_W - 1);

  int currentSegs = levelToSegments(levelPx);
  int segW = (BAR_W - (SEGMENTS - 1) * SEG_GAP) / SEGMENTS;

  // نرسم فقط الأجزاء التي تغيرت حتى تكون الشاشة أسرع وأقل وميضاً
  if (currentSegs != lastSegs[meterIndex]) {
    if (lastSegs[meterIndex] < 0) lastSegs[meterIndex] = 0;

    if (currentSegs > lastSegs[meterIndex]) {
      for (int i = lastSegs[meterIndex]; i < currentSegs; i++) {
        int x = BAR_X + i * (segW + SEG_GAP);
        tft.fillRoundRect(x, barY, segW, BAR_H, 2, segmentColorByIndex(i));
      }
    } else {
      for (int i = currentSegs; i < lastSegs[meterIndex]; i++) {
        int x = BAR_X + i * (segW + SEG_GAP);
        tft.fillRoundRect(x, barY, segW, BAR_H, 2, COLOR_OFFSEG);
      }
    }

    lastSegs[meterIndex] = currentSegs;
  }

  if (!ENABLE_PEAK_LINE) return;

  // حذف خط الـ Peak القديم وإعادة رسم الجزء الذي كان تحته
  if (lastPeakPx[meterIndex] >= 0 && lastPeakPx[meterIndex] != peakPx) {
    tft.drawFastVLine(BAR_X + lastPeakPx[meterIndex], barY - 8, BAR_H + 16, COLOR_PANEL);
    redrawSegmentAtPixel(barY, lastPeakPx[meterIndex], currentSegs);

    for (int i = 1; i < 10; i++) {
      int x = (BAR_W * i) / 10;

      if (x == lastPeakPx[meterIndex]) {
        tft.drawFastVLine(BAR_X + x, barY - 2, BAR_H + 4, COLOR_GRID);
      }
    }

    tft.drawRoundRect(BAR_X - 6, barY - 6, BAR_W + 12, BAR_H + 12, 8, COLOR_FRAME);
  }

  if (peakPx > 1) {
    tft.drawFastVLine(BAR_X + peakPx, barY - 8, BAR_H + 16, COLOR_PEAK);
  }

  lastPeakPx[meterIndex] = peakPx;
}

void drawPercents(int bassPercent, int voicePercent) {
  static int lastBassPercent = -1;
  static int lastVoicePercent = -1;

  if (bassPercent == lastBassPercent && voicePercent == lastVoicePercent) return;

  tft.fillRect(0, PERCENT_Y, SCREEN_W, 24, COLOR_BG);

  tft.setTextSize(2);

  tft.setTextColor(COLOR_LOW);
  tft.setCursor(38, PERCENT_Y);
  tft.print("B:");
  tft.print(bassPercent);
  tft.print("%");

  tft.setTextColor(COLOR_SUBTEXT);
  tft.setCursor(170, PERCENT_Y);
  tft.print("T:");
  tft.print(voicePercent);
  tft.print("%");

  lastBassPercent = bassPercent;
  lastVoicePercent = voicePercent;
}

// =====================================================
// MIC SETUP / تهيئة مايكروفون I2S
// =====================================================
void setupMic() {
  i2s_config_t i2s_config = {};
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2s_config.sample_rate = SAMPLE_RATE;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  i2s_config.channel_format = MIC_CHANNEL_FORMAT;
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_config.dma_buf_count = 8;
  i2s_config.dma_buf_len = 256;
  i2s_config.use_apll = false;
  i2s_config.tx_desc_auto_clear = false;
  i2s_config.fixed_mclk = 0;

  i2s_pin_config_t pin_config = {};
  pin_config.bck_io_num = I2S_SCK;
  pin_config.ws_io_num = I2S_WS;
  pin_config.data_out_num = -1;
  pin_config.data_in_num = I2S_SD;

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

// =====================================================
// FILTER SETUP / تهيئة فلاتر الصوت
// =====================================================
void setupAudioFilters() {
  const float Q = 0.707f;

  // فلترة نطاق Voice/Treble
  voiceHP1.setHighPass(SAMPLE_RATE, VOICE_LOW_CUT_HZ, Q);
  voiceHP2.setHighPass(SAMPLE_RATE, VOICE_LOW_CUT_HZ, Q);

  voiceLP1.setLowPass(SAMPLE_RATE, VOICE_HIGH_CUT_HZ, Q);
  voiceLP2.setLowPass(SAMPLE_RATE, VOICE_HIGH_CUT_HZ, Q);

  // فلترة الترددات العالية جداً لإضافة إحساس التريبل
  trebleHP1.setHighPass(SAMPLE_RATE, TREBLE_CUT_HZ, Q);
  trebleHP2.setHighPass(SAMPLE_RATE, TREBLE_CUT_HZ, Q);
}

// =====================================================
// READ MIC LEVELS / قراءة الصوت وحساب مستويات البيس والتريبل
// =====================================================
AudioLevels readMicLevels() {
  AudioLevels out;
  out.bassPx = 0;
  out.voicePx = 0;

  size_t bytesRead = 0;

  // قراءة دفعة من عينات الصوت من I2S
  esp_err_t ok = i2s_read(
    I2S_PORT,
    (void*)i2sSamples,
    sizeof(i2sSamples),
    &bytesRead,
    portMAX_DELAY
  );

  if (ok != ESP_OK || bytesRead == 0) return out;

  int count = bytesRead / sizeof(int32_t);
  if (count <= 0) return out;

  // حساب المتوسط لإزالة DC offset من الإشارة
  double mean = 0.0;

  for (int i = 0; i < count; i++) {
    int32_t s = i2sSamples[i] >> MIC_BIT_SHIFT;
    mean += s;
  }

  mean /= count;

  double bassSumSq = 0.0;
  double voiceSumSq = 0.0;
  double trebleSumSq = 0.0;

  for (int i = 0; i < count; i++) {
    float x = (float)((i2sSamples[i] >> MIC_BIT_SHIFT) - mean);

    // Bass هنا يعتمد على RMS للإشارة الأصلية بعد إزالة المتوسط
    bassSumSq += (double)x * (double)x;

    // نطاق Voice: High-pass ثم Low-pass
    float voiceBand = voiceHP1.process(x);
    voiceBand = voiceHP2.process(voiceBand);
    voiceBand = voiceLP1.process(voiceBand);
    voiceBand = voiceLP2.process(voiceBand);

    // نطاق Treble: High-pass فقط
    float treble = trebleHP1.process(x);
    treble = trebleHP2.process(treble);

    voiceSumSq += (double)voiceBand * (double)voiceBand;
    trebleSumSq += (double)treble * (double)treble;
  }

  float bassRms = sqrt(bassSumSq / count);

  float voiceRms = sqrt(
    ((voiceSumSq / count) * VOICE_WEIGHT) +
    ((trebleSumSq / count) * TREBLE_WEIGHT)
  );

  out.bassPx = bassOriginalRmsToLevel(bassRms);
  out.voicePx = voiceTrebleToLevel(voiceRms);

  if (DEBUG_SERIAL_LEVELS) {
    Serial.print("Bass RMS: ");
    Serial.print(bassRms);
    Serial.print("  Treble RMS: ");
    Serial.print(voiceRms);
    Serial.print("  BassPx: ");
    Serial.print(out.bassPx);
    Serial.print("  TreblePx: ");
    Serial.println(out.voicePx);
  }

  return out;
}

// =====================================================
// SETUP / تعمل مرة واحدة عند تشغيل ESP32
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  // تشغيل إضاءة خلفية الشاشة
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  setupRgbLed();

  // بدء SPI للشاشة باستخدام الأرجل المحددة فوق
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  // الشاشة 240x320، ثم تدويرها Landscape
  tft.init(240, 320);
  tft.setRotation(1);

  setupAudioFilters();
  setupMic();

  drawStaticUI();

  lastDrawUs = micros();
}

// =====================================================
// LOOP / الحلقة الرئيسية
// =====================================================
void loop() {
  // قراءة مستويات الصوت من المايك
  AudioLevels raw = readMicLevels();

  // تحديث الليد حسب Bass و Treble
  updateSmartRgbLed(raw.bassPx, raw.voicePx);

  // تنعيم حركة العدادات
  smoothMeter(raw.bassPx, bassDisplayLevel);
  smoothMeter(raw.voicePx, voiceDisplayLevel);

  // تحديث خطوط Peak
  updatePeak(bassDisplayLevel, bassPeakLevel, bassLastPeakHold, bassLastPeakFall);
  updatePeak(voiceDisplayLevel, voicePeakLevel, voiceLastPeakHold, voiceLastPeakFall);

  if (raw.bassPx == 0 && bassDisplayLevel == 0.0f) {
    bassPeakLevel = 0.0f;
  }

  if (raw.voicePx == 0 && voiceDisplayLevel == 0.0f) {
    voicePeakLevel = 0.0f;
  }

  uint32_t nowUs = micros();

  // الرسم يتم حسب TARGET_FPS وليس بكل دورة حتى تبقى الحركة ناعمة
  if ((uint32_t)(nowUs - lastDrawUs) >= FRAME_INTERVAL_US) {
    int bassShown  = constrain((int)bassDisplayLevel, 0, BAR_W);
    int voiceShown = constrain((int)voiceDisplayLevel, 0, BAR_W);

    int bassPeak   = constrain((int)bassPeakLevel, 0, BAR_W - 1);
    int voicePeak  = constrain((int)voicePeakLevel, 0, BAR_W - 1);

    int bassPercent  = levelToPercent(bassShown);
    int voicePercent = levelToPercent(voiceShown);

    drawMeter(0, BAR_Y_VOICE, voiceShown, voicePeak);
    drawMeter(1, BAR_Y_BASS, bassShown, bassPeak);

    drawPercents(bassPercent, voicePercent);

    lastDrawUs = nowUs;
  }

  // يعطي فرصة للـ ESP32 background tasks بدون تأخير فعلي
  delay(0);
}
