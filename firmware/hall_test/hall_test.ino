// ════════════════════════════════════════════════════════════════════════════
//  hall_test.ino  —  SS49E Hall Sensor Standalone Test
//  Target : Arduino Nano (ATmega328P @ 16 MHz)
//
//  Wiring:
//    SS49E Pin 1 VCC -> Arduino 5V
//    SS49E Pin 2 GND -> Arduino GND
//    SS49E Pin 3 OUT -> 1kOhm -> A0  (100nF to GND after resistor)
//
//  Open Serial Monitor at 115200 baud.
//
//  HOW TO READ:
//    No magnet/ball nearby  -> ADC ~512  (sensor at midpoint = 2.5V)
//    Ball closer to sensor  -> ADC rises toward 1023
//    Ball farther away      -> ADC falls toward 0
//    Use the bar [====    ] to see changes visually.
// ════════════════════════════════════════════════════════════════════════════

#include <avr/io.h>

#define HALL_PIN     A0
#define OVERSAMPLE   16       // ADC reads averaged per sample
#define PRINT_MS    100       // print interval (ms)
#define BAR_WIDTH    40       // width of ASCII bar graph

// ── Supply voltage ────────────────────────────────────────────────────────
// Set to match the actual VCC you wired to the SS49E.
// SS49E MINIMUM is 4.5V — 3.3V is BELOW SPEC (see warning at startup).
#define SENSOR_VCC   4.11f   // VIN pin measured — change to 5.0f when on 5V pin

// At SENSOR_VCC the SS49E output range is 0 .. SENSOR_VCC.
// On a 5V-AREF Nano: max ADC = SENSOR_VCC / 5.0 * 1023
#define ADC_MAX_EXPECTED  (int)(SENSOR_VCC / 5.0f * 1023.0f)   // ~675 at 3.3V
#define ADC_MID_EXPECTED  (int)(ADC_MAX_EXPECTED / 2)           // ~338 at 3.3V

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(HALL_PIN, INPUT);

  // Speed up ADC: prescaler 16 -> 1 MHz ADC clock
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
  ADCSRA = (ADCSRA & 0xF8) | 0x04;
#endif

  // Warm up ADC
  for (uint8_t i = 0; i < 16; i++) { analogRead(HALL_PIN); delayMicroseconds(50); }

  Serial.println(F("# ─────────────────────────────────────────────"));
  Serial.println(F("# SS49E Hall Sensor Test"));
  if (SENSOR_VCC < 4.5f) {
    Serial.println(F("# !! WARNING: SENSOR_VCC < 4.5V !!"));
    Serial.println(F("# SS49E minimum supply = 4.5V. At 3.3V readings"));
    Serial.println(F("# will be weak and unreliable. Move VCC to 5V pin."));
  }
  Serial.print  (F("# Sensor VCC = ")); Serial.print(SENSOR_VCC, 1); Serial.println(F("V"));
  Serial.print  (F("# No-field ADC expected ~ ")); Serial.println(ADC_MID_EXPECTED);
  Serial.print  (F("# Max ADC expected      ~ ")); Serial.println(ADC_MAX_EXPECTED);
  Serial.println(F("# ─────────────────────────────────────────────"));
  Serial.println(F("# ADC    Volts   Bar"));
  Serial.println(F("# ─────────────────────────────────────────────"));
}

void loop() {
  // Oversample
  uint32_t sum = 0;
  for (uint8_t i = 0; i < OVERSAMPLE; i++) {
    sum += (uint16_t)analogRead(HALL_PIN);
  }
  float adc   = (float)sum / (float)OVERSAMPLE;
  float volts = adc * (5.0f / 1023.0f);   // actual voltage on A0 pin

  // ── Print ADC value & voltage ─────────────────────────────────────────────
  Serial.print(F("  "));
  // Pad ADC to 4 chars
  if (adc < 1000) Serial.print(' ');
  if (adc < 100)  Serial.print(' ');
  if (adc < 10)   Serial.print(' ');
  Serial.print((int)adc);

  Serial.print(F("   "));
  Serial.print(volts, 3);
  Serial.print(F("V  ["));

  // ── ASCII bar graph ───────────────────────────────────────────────────────
  // Map ADC 0..ADC_MAX_EXPECTED to BAR_WIDTH chars
  int filled = (int)(adc / (float)ADC_MAX_EXPECTED * BAR_WIDTH);
  if (filled > BAR_WIDTH) filled = BAR_WIDTH;
  for (int i = 0; i < BAR_WIDTH; i++) {
    Serial.print(i < filled ? '=' : ' ');
  }
  Serial.print(F("]"));

  // ── Tag: relative to expected midpoint ──────────────────────────────────
  int mid  = ADC_MID_EXPECTED;
  int diff = (int)adc - mid;
  if (diff > -20 && diff < 20) {
    Serial.print(F("  ~ no field"));
  } else if (diff >= 20 && diff < (mid / 3)) {
    Serial.print(F("  + weak field"));
  } else if (diff >= (mid / 3) && diff < (mid * 2 / 3)) {
    Serial.print(F("  ++ medium field"));
  } else if (diff >= (mid * 2 / 3)) {
    Serial.print(F("  +++ STRONG / CLOSE"));
  } else if (diff < -20 && diff > -(mid / 3)) {
    Serial.print(F("  - opposite/weak"));
  } else if (diff <= -(mid / 3)) {
    Serial.print(F("  -- opposite/strong"));
  }

  Serial.println();
  delay(PRINT_MS);
}
