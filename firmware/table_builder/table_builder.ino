// ════════════════════════════════════════════════════════════════════════════
//  table_builder.ino  —  Lavitator Interference Calibrator
//  Runs the coil from 0 to 511 duty (9-bit) and maps how much the coil's
//  own magnetic field affects the Hall sensor. 
//  OUTPUT: A ready-to-paste C++ array.
// ════════════════════════════════════════════════════════════════════════════

#include <avr/io.h>

#define HALL_PIN   A0
#define PWM_PIN     9

void setup() {
  Serial.begin(115200);
  
  // Setup PWM (9-bit, 31kHz)
  pinMode(PWM_PIN, OUTPUT);
  TCCR1A = _BV(COM1A1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
  ICR1   = 511;
  OCR1A  = 0;

  // Fast ADC
  pinMode(HALL_PIN, INPUT);
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
  ADCSRA = (ADCSRA & 0xF8) | 0x04;
#endif

  Serial.println(F("\n\n# ────────────────────────────────────────────"));
  Serial.println(F("# CALIBRATING ELECTROMAGNET INTERFERENCE"));
  Serial.println(F("# 1. REMOVE THE BALL completely."));
  Serial.println(F("# 2. Send 'C' in Serial Monitor to begin."));
  Serial.println(F("# ────────────────────────────────────────────"));
}

void loop() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'C' || c == 'c') {
      runCalibration();
    }
  }
}

void runCalibration() {
  Serial.println(F("# Calibrating... (takes ~5 seconds)"));
  
  int offsetTable[256];
  
  // Ramp PWM in 256 steps (0 to 511, by 2s)
  for (int step = 0; step < 256; step++) {
    int pwmDuty = step * 2;
    OCR1A = pwmDuty;
    
    delay(20); // Let magnetic field settle
    
    // Average 64 readings
    long sum = 0;
    for (int i = 0; i < 64; i++) {
      sum += analogRead(HALL_PIN);
      delayMicroseconds(50);
    }
    
    offsetTable[step] = sum / 64;
  }
  
  OCR1A = 0; // Turn coil off
  
  // Print the array for copy-pasting
  Serial.println(F("\n// Copy this block into maglev_simple.ino:"));
  Serial.println(F("static const int offsetTable[256] = {"));
  
  for (int i = 0; i < 256; i++) {
    if (i % 10 == 0) {
      Serial.print(F("  "));
    }
    Serial.print(offsetTable[i]);
    if (i < 255) Serial.print(F(","));
    else         Serial.print(F(" "));
    
    if (i % 10 == 9 || i == 255) {
      Serial.println();
    }
  }
  Serial.println(F("};"));
  Serial.println(F("\n# Done! Copy the block above."));
}
