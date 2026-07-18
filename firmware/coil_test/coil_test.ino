// ════════════════════════════════════════════════════════════════════════════
//  coil_test.ino  —  Electromagnet / MOSFET Standalone Test
//  Target : Arduino Nano (ATmega328P @ 16 MHz)
//
//  Wiring (same as main project):
//    D9 -> 150Ohm -> MOSFET Gate  (+ 10kOhm pull-down Gate-to-GND)
//    Coil across MOSFET Drain/Source with flyback diode installed
//    12V supply to coil (DO NOT run without flyback diode)
//
//  Serial commands (115200 baud):
//    0..9    -> set duty 0%, 11%, 22% ... 100%  (quick steps)
//    +       -> increase duty by 5
//    -       -> decrease duty by 5
//    S<val>  -> set exact duty 0-480  e.g. "S240"
//    R       -> ramp up slowly then back down (auto demo)
//    X       -> emergency stop (duty = 0)
//
//  SAFETY:
//    - Flyback diode MUST be installed before applying 12V
//    - Do not run at full duty (480) for more than a few seconds
//    - If coil gets too hot to touch -> send X immediately
// ════════════════════════════════════════════════════════════════════════════

#include <avr/io.h>

#define PWM_PIN       9
#define LED_PIN      13
#define PWM_MAX      511    // ICR1 top (9-bit, 31 kHz)
#define PWM_SOFT_LIMIT 480  // ~94% max — never go higher
#define SERIAL_BAUD  115200UL

// ─────────────────────────────────────────────────────────────────────────────

uint16_t g_duty = 0;

// Timer1 31 kHz PWM setup (same as main firmware)
void pwmBegin() {
  pinMode(PWM_PIN, OUTPUT);
  digitalWrite(PWM_PIN, LOW);
  TCCR1A = _BV(COM1A1) | _BV(WGM11);
  TCCR1B = _BV(WGM13)  | _BV(WGM12) | _BV(CS10);
  ICR1   = (uint16_t)PWM_MAX;
  OCR1A  = 0;
}

void pwmSet(uint16_t duty) {
  if (duty > PWM_SOFT_LIMIT) duty = PWM_SOFT_LIMIT;
  g_duty = duty;
  OCR1A  = duty;
}

void pwmStop() {
  g_duty = 0;
  OCR1A  = 0;
}

void printStatus() {
  uint8_t pct = (uint8_t)((uint32_t)g_duty * 100UL / PWM_SOFT_LIMIT);
  Serial.print(F("Duty: "));
  Serial.print(g_duty);
  Serial.print(F(" / 480  ("));
  Serial.print(pct);
  Serial.print(F("%)  "));

  // Mini bar
  Serial.print('[');
  uint8_t bars = pct / 5;   // 20 chars wide
  for (uint8_t i = 0; i < 20; i++) Serial.print(i < bars ? '=' : ' ');
  Serial.println(']');
}

void doRamp() {
  Serial.println(F("# Ramping UP..."));
  for (uint16_t d = 0; d <= PWM_SOFT_LIMIT; d += 10) {
    pwmSet(d);
    printStatus();
    delay(80);
  }
  delay(500);
  Serial.println(F("# Ramping DOWN..."));
  for (int16_t d = PWM_SOFT_LIMIT; d >= 0; d -= 10) {
    pwmSet((uint16_t)d);
    printStatus();
    delay(80);
  }
  pwmStop();
  Serial.println(F("# Ramp done."));
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(SERIAL_BAUD);
  pwmBegin();
  pinMode(LED_PIN, OUTPUT);

  Serial.println(F("# ────────────────────────────────────────────────"));
  Serial.println(F("# Electromagnet / MOSFET Test"));
  Serial.println(F("# ENSURE flyback diode is installed before 12V!"));
  Serial.println(F("# ────────────────────────────────────────────────"));
  Serial.println(F("# Commands:"));
  Serial.println(F("#   0-9  -> duty preset (0%, 11%, ..., 100%)"));
  Serial.println(F("#   +/-  -> duty +5 / -5"));
  Serial.println(F("#   S<n> -> set exact duty 0-480  e.g. S240"));
  Serial.println(F("#   R    -> slow ramp up/down demo"));
  Serial.println(F("#   X    -> EMERGENCY STOP (duty=0)"));
  Serial.println(F("# ────────────────────────────────────────────────"));
  Serial.println(F("# Duty: 0 / 480  (0%)  [                    ]"));
}

static char   cmdBuf[12];
static uint8_t cmdIdx = 0;

void loop() {
  // LED mirrors coil: ON when duty > 0
  digitalWrite(LED_PIN, g_duty > 0 ? HIGH : LOW);

  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    // Single-char instant commands
    if (c == '+') {
      uint16_t nd = g_duty + 5;
      if (nd > PWM_SOFT_LIMIT) nd = PWM_SOFT_LIMIT;
      pwmSet(nd); printStatus(); continue;
    }
    if (c == '-') {
      int16_t nd = (int16_t)g_duty - 5;
      if (nd < 0) nd = 0;
      pwmSet((uint16_t)nd); printStatus(); continue;
    }
    if (c >= '0' && c <= '9') {
      uint16_t nd = (uint16_t)((c - '0') * PWM_SOFT_LIMIT / 9);
      pwmSet(nd); printStatus(); continue;
    }
    if (c == 'X' || c == 'x') {
      pwmStop();
      Serial.println(F("# EMERGENCY STOP — duty = 0"));
      printStatus(); continue;
    }
    if (c == 'R' || c == 'r') {
      doRamp(); continue;
    }

    // Buffered command (S<val>)
    if (c == '\n' || c == '\r') {
      if (cmdIdx > 0) {
        cmdBuf[cmdIdx] = '\0';
        if (cmdBuf[0] == 'S' || cmdBuf[0] == 's') {
          uint16_t val = (uint16_t)atoi(cmdBuf + 1);
          pwmSet(val);
          printStatus();
        }
        cmdIdx = 0;
      }
    } else {
      if (cmdIdx < sizeof(cmdBuf) - 1) cmdBuf[cmdIdx++] = c;
    }
  }
}
