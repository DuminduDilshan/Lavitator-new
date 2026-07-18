// ════════════════════════════════════════════════════════════════════════════
//  autotuner.ino  —  Lavitator PID Auto-Tuner (Relay / Ziegler-Nichols)
//  Target : Arduino Nano (ATmega328P @ 16 MHz)
//
//  HOW IT WORKS:
//    1. Upload this sketch
//    2. Hold ball at levitation gap, then send  T  to start
//    3. The coil switches between HIGH and LOW duty (relay control)
//    4. The ball oscillates — the sketch measures frequency & amplitude
//    5. After 8 oscillation cycles it computes Kp and Kd automatically
//    6. Prints: "# RESULT Kp=X.XX Kd=Y.YY" — copy into maglev_simple.ino
//
//  Serial commands (115200 baud, Newline):
//    T     — start relay auto-tune (hold ball at gap first!)
//    X     — abort / stop
//    S346  — set setpoint ADC (default 346)
//
//  CSV output: time_ms, sensor_raw, output, state
// ════════════════════════════════════════════════════════════════════════════

#include <avr/io.h>
#include <math.h>      // fabs()
#include <stdlib.h>    // atof()

// ── Hardware ──────────────────────────────────────────────────────────────────
#define HALL_PIN          A0
#define PWM_PIN            9
#define LED_PIN           13

#define PWM_MAX          511
#define PWM_SOFT_LIMIT   480
#define LOOP_US          1000UL
#define SERIAL_BAUD      115200UL
#define HALL_OVERSAMPLE   16

// ── Tuner settings ────────────────────────────────────────────────────────────
#define SETPOINT          346.0f   // your measured levitation ADC
#define PWM_BIAS          265.0f   // your measured equilibrium duty
#define RELAY_AMP          50.0f   // relay step ± this value around bias
#define SETTLE_MS         2500UL   // wait for system to settle before relay starts
#define TUNE_CROSSINGS      16     // half-cycles to average (8 full cycles)
#define SAFETY_LOW         150     // ADC below this = contact → abort
#define SAFETY_HIGH        490     // ADC above this = fallen → abort

// ── State machine ────────────────────────────────────────────────────────
#define ST_IDLE     0
#define ST_SETTLING 1
#define ST_RELAY    2
#define ST_DONE     3
uint8_t g_state = ST_IDLE;

// ── Relay tracking ────────────────────────────────────────────────────────────
uint8_t  g_crossings      = 0;
uint32_t g_lastCrossMs    = 0;
float    g_halfPeriodSum  = 0.0f;   // sum of half-period durations (ms)
float    g_peakMax        = 0.0f;   // max ADC seen since last crossing
float    g_peakMin        = 1023.0f;// min ADC seen since last crossing
float    g_ampSum         = 0.0f;   // sum of half-amplitudes
bool     g_lastAbove      = false;  // was sensor above setpoint last tick?
uint32_t g_settleStart    = 0;

// Results
float g_resultKp = 0, g_resultKd = 0;

// ── Loop timing ───────────────────────────────────────────────────────────────
uint32_t g_lastLoopUs = 0;

// ── Command buffer ────────────────────────────────────────────────────────────
static char    g_cmdBuf[12];
static uint8_t g_cmdIdx = 0;
float g_setpoint = SETPOINT;

// ── Interference Canceller ───────────────────────────────────────────────────
// Calibrated coil interference table (maps 9-bit PWM to raw ADC drop)
static const int offsetTable[256] = {
  455,455,455,455,455,455,455,455,455,455,
  455,455,455,455,455,455,455,455,455,455,
  455,455,455,455,455,455,455,455,455,455,
  455,455,455,455,455,455,455,455,455,455,
  455,455,455,455,455,455,455,455,455,455,
  455,455,455,455,455,455,455,455,455,455,
  455,455,455,455,455,455,455,455,455,455,
  455,455,455,455,455,455,455,455,455,455,
  455,455,455,455,455,455,455,455,455,455,
  455,455,455,455,455,455,454,455,455,454,
  454,454,453,453,452,451,450,449,448,447,
  445,443,440,439,435,431,427,422,415,411,
  404,397,390,383,375,367,358,350,342,334,
  326,319,314,310,306,304,303,301,300,299,
  299,298,297,297,296,297,296,296,296,295,
  295,295,295,295,295,295,295,295,295,295,
  295,295,295,295,295,295,295,295,295,295,
  295,295,295,295,294,295,295,295,295,295,
  295,295,295,295,294,295,295,295,294,295,
  294,295,294,295,294,294,295,295,294,295,
  295,294,294,295,295,295,294,294,295,295,
  294,294,295,294,294,294,294,294,295,294,
  295,295,295,294,295,294,294,294,295,294,
  294,295,295,295,295,294,294,295,294,294,
  294,295,295,294,294,295,294,294,294,294,
  295,295,295,294,295,295
};

float calcExpectedOffset(uint16_t duty9bit) {
  int pwm = (int)(duty9bit / 2);
  if (pwm > 255) pwm = 255;
  
  // Average 5 surrounding points to smooth out the curve
  int start = pwm - 2; if (start < 0)   start = 0;
  int end   = pwm + 2; if (end   > 255) end   = 255;
  
  long sum = 0; 
  int count = 0;
  for (int i = start; i <= end; i++) { 
    sum += offsetTable[i]; 
    count++; 
  }
  return (float)sum / (float)count;
}

uint16_t g_lastDuty = 0;

// ─────────────────────────────────────────────────────────────────────────────

void pwmBegin() {
  pinMode(PWM_PIN, OUTPUT);
  digitalWrite(PWM_PIN, LOW);
  TCCR1A = _BV(COM1A1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
  ICR1   = (uint16_t)PWM_MAX;
  OCR1A  = 0;
}

void pwmSet(float duty) {
  int16_t d = (int16_t)duty;
  if (d < 0)              d = 0;
  if (d > PWM_SOFT_LIMIT) d = PWM_SOFT_LIMIT;
  OCR1A = (uint16_t)d;
  g_lastDuty = (uint16_t)d;
}

void pwmStop() { 
  OCR1A = 0; 
  g_lastDuty = 0;
}

void hallBegin() {
  pinMode(HALL_PIN, INPUT);
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
  ADCSRA = (ADCSRA & 0xF8) | 0x04;
#endif
  for (uint8_t i = 0; i < 16; i++) { analogRead(HALL_PIN); delayMicroseconds(50); }
}

float hallRead() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < HALL_OVERSAMPLE; i++) sum += (uint16_t)analogRead(HALL_PIN);
  return (float)sum / (float)HALL_OVERSAMPLE;
}

void resetRelay() {
  g_crossings     = 0;
  g_halfPeriodSum = 0.0f;
  g_peakMax       = 0.0f;
  g_peakMin       = 1023.0f;
  g_ampSum        = 0.0f;
  g_lastCrossMs   = millis();
}

void computeResults() {
  // Average oscillation period (full cycle)
  float Tu = 2.0f * (g_halfPeriodSum / (float)(g_crossings - 1)) / 1000.0f; // seconds

  // Average amplitude (peak-to-peak / 2)
  float a  = g_ampSum / (float)(g_crossings - 1);

  // Ultimate gain: Ku = (4 * relay_amplitude) / (π * oscillation_amplitude)
  float Ku = (4.0f * RELAY_AMP) / (3.14159f * a);

  // Conservative Z-N (no-overshoot variant):
  g_resultKp = 0.33f * Ku;
  g_resultKd = 0.08f * Ku * Tu;

  Serial.println(F("# ────────────────────────────────────────────────"));
  Serial.println(F("# AUTO-TUNE COMPLETE"));
  Serial.print  (F("# Ultimate gain   Ku = ")); Serial.println(Ku, 4);
  Serial.print  (F("# Ultimate period Tu = ")); Serial.print(Tu, 4); Serial.println(F(" s"));
  Serial.print  (F("# Oscillation amp  a = ")); Serial.print(a, 2); Serial.println(F(" ADC"));
  Serial.println(F("# ────────────────────────────────────────────────"));
  Serial.print  (F("# RESULT Kp=")); Serial.print(g_resultKp, 4);
  Serial.print  (F(" Kd="));         Serial.println(g_resultKd, 4);
  Serial.println(F("# ────────────────────────────────────────────────"));
  Serial.println(F("# Copy these values into maglev_simple.ino:"));
  Serial.print  (F("#   #define KP_DEFAULT  ")); Serial.println(g_resultKp, 4);
  Serial.print  (F("#   #define KD_DEFAULT  ")); Serial.println(g_resultKd, 4);
  Serial.println(F("# ────────────────────────────────────────────────"));
}

void processSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (g_cmdIdx > 0) {
        g_cmdBuf[g_cmdIdx] = '\0';
        char  cmd = g_cmdBuf[0];
        float val = (g_cmdIdx > 1) ? atof(g_cmdBuf + 1) : 0.0f;
        switch (cmd) {
          case 'T': case 't':
            if (g_state == ST_IDLE) {
              Serial.println(F("# Settling... hold ball at gap."));
              g_settleStart = millis();
              pwmSet(PWM_BIAS);
              resetRelay();
              g_state = ST_SETTLING;
            } else {
              Serial.println(F("# Already running. Send X to abort first."));
            }
            break;
          case 'X': case 'x':
            pwmStop();
            g_state = ST_IDLE;
            Serial.println(F("# Aborted."));
            break;
          case 'S': case 's':
            g_setpoint = val;
            Serial.print(F("# Setpoint = ")); Serial.println(val, 1);
            break;
          default:
            break;
        }
        g_cmdIdx = 0;
      }
    } else {
      if (g_cmdIdx < sizeof(g_cmdBuf) - 1) g_cmdBuf[g_cmdIdx++] = c;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(SERIAL_BAUD);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000UL) {}

  hallBegin();
  pwmBegin();
  pinMode(LED_PIN, OUTPUT);

  Serial.println(F("# ════════════════════════════════════════════════"));
  Serial.println(F("# Lavitator PID Auto-Tuner (Relay / Ziegler-Nichols)"));
  Serial.println(F("# Hold ball at levitation gap, then send:  T"));
  Serial.println(F("# S<val> = set setpoint (default 346)"));
  Serial.println(F("# X = abort"));
  Serial.println(F("# ════════════════════════════════════════════════"));
  Serial.println(F("time_ms,sensor_raw,output,state"));

  g_lastLoopUs = micros();
}

void loop() {
  uint32_t now     = micros();
  uint32_t elapsed = now - g_lastLoopUs;
  if (elapsed < LOOP_US) return;
  g_lastLoopUs = now;

  processSerial();

  // Read raw sensor and cancel out the coil's magnetic field
  float sensorRaw = hallRead();
  float expected  = calcExpectedOffset(g_lastDuty);
  
  // cleanSignal = Raw - Expected + Baseline (455)
  // This means if there is NO ball, cleanSignal is ALWAYS 455 regardless of PWM.
  // When the ball is near, cleanSignal will change and only reflect the ball!
  float cleanSignal = sensorRaw - expected + 455.0f;
  
  float output = 0.0f;
  uint8_t stateCode = 0;

  switch (g_state) {

    case ST_IDLE:
      pwmStop();
      stateCode = 0;
      break;

    case ST_SETTLING:
      stateCode = 1;
      output = PWM_BIAS;
      pwmSet(output);
      if (millis() - g_settleStart >= SETTLE_MS) {
        // Check sensor is near setpoint before starting relay
        if ((cleanSignal > g_setpoint ? cleanSignal - g_setpoint : g_setpoint - cleanSignal) < 60.0f) {
          Serial.println(F("# Relay starting..."));
          g_lastAbove    = (cleanSignal > g_setpoint);
          g_lastCrossMs  = millis();
          g_peakMax      = cleanSignal;
          g_peakMin      = cleanSignal;
          g_state        = ST_RELAY;
        } else {
          Serial.print(F("# Sensor not near setpoint ("));
          Serial.print(cleanSignal, 0);
          Serial.println(F("). Re-position ball or send S<new_val>, then T again."));
          pwmStop();
          g_state = ST_IDLE;
        }
      }
      break;

    case ST_RELAY: {
      stateCode = 2;

      // Safety check disabled during tuning for now, to avoid false aborts
      // We rely on the user to hit X if the ball falls.

      // Relay control: switch based on which side of setpoint we are
      bool above = (cleanSignal > g_setpoint);
      output = PWM_BIAS + (above ? -RELAY_AMP : +RELAY_AMP);
      pwmSet(output);

      // Track peak in current half-cycle
      if (cleanSignal > g_peakMax) g_peakMax = cleanSignal;
      if (cleanSignal < g_peakMin) g_peakMin = cleanSignal;

      // Detect zero-crossing (sensor crosses setpoint)
      if (above != g_lastAbove) {
        uint32_t now_ms       = millis();
        float    halfPeriodMs = (float)(now_ms - g_lastCrossMs);
        float    halfAmp      = (g_peakMax - g_peakMin);

        if (g_crossings > 0) {   // skip first (incomplete) half-cycle
          g_halfPeriodSum += halfPeriodMs;
          g_ampSum        += halfAmp;
          Serial.print(F("# Cycle half "));
          Serial.print(g_crossings);
          Serial.print(F("  half-T="));
          Serial.print(halfPeriodMs, 0);
          Serial.print(F("ms  amp="));
          Serial.println(halfAmp, 1);
        }

        // Reset peak tracking for next half-cycle
        g_peakMax      = cleanSignal;
        g_peakMin      = cleanSignal;
        g_lastCrossMs  = now_ms;
        g_lastAbove    = above;
        g_crossings++;

        if (g_crossings >= TUNE_CROSSINGS) {
          pwmStop();
          g_state = ST_DONE;
        }
      }
      break;
    }

    case ST_DONE:
      stateCode = 3;
      pwmStop();
      computeResults();
      g_state = ST_IDLE;
      break;
  }

  // LED: blinks fast during relay, slow when idle
  static uint16_t ledCount = 0;
  uint16_t ledPeriod = (g_state == ST_RELAY) ? (uint16_t)100 : (uint16_t)500;
  if (++ledCount >= ledPeriod) {
    ledCount = 0;
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }

  // CSV output every 5 cycles (~200Hz) for smooth Python plot
  static uint8_t logCount = 0;
  if (++logCount >= 5) {
    logCount = 0;
    Serial.print(millis());         Serial.print(',');
    Serial.print(cleanSignal, 1);   Serial.print(',');
    Serial.print(output, 1);        Serial.print(',');
    Serial.println(stateCode);
  }
}
