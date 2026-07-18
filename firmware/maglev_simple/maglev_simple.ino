// ════════════════════════════════════════════════════════════════════════════
//  maglev_simple.ino  —  Lavitator Maglev Controller  (single-file edition)
//  Target : Arduino Nano (ATmega328P @ 16 MHz)
//
//  Everything is in this one file — just open and upload.
//
//  Wiring:
//    A0  <- SS49E signal  (1kOhm + 100nF RC filter to GND)
//    D9  -> 150Ohm -> MOSFET Gate  (+ 10kOhm pull-down Gate-to-GND)
//    Flyback diode: cathode -> +12V rail, anode -> Drain/Coil-
//
//  Serial commands (115200 baud, newline-terminated):
//    P2.5   set Kp        D1.8   set Kd        I0.05  set Ki
//    S346   set Setpoint  R      reset/re-arm
//    M265   manual PWM    A      return to auto
//
//  CSV output: time_ms, sensor_raw, error, output, kp, ki, kd
//
//  CONTROL DIRECTION (confirmed from hardware test):
//    ADC rises above setpoint -> ball too close -> reduce PWM
//    ADC drops below setpoint -> ball too far   -> increase PWM
//    error = setpoint - measurement
// ════════════════════════════════════════════════════════════════════════════

#include <avr/io.h>
#include <stdlib.h>

// ── Pin & timing ──────────────────────────────────────────────────────────────
#define HALL_PIN          A0
#define PWM_PIN            9
#define LED_PIN           13

#define PWM_MAX          511      // ICR1 top (9-bit, 31 kHz)
#define PWM_SOFT_LIMIT   480      // ~94% max duty
#define LOOP_INTERVAL_US 1000UL   // 1 kHz control loop

// ── Sensor ───────────────────────────────────────────────────────────────────
#define HALL_OVERSAMPLE   16      // ADC reads averaged per tick

// ── Setpoint & PID ───────────────────────────────────────────────────────────
// SETPOINT_ADC: your cleanSignal value at the levitation gap
#define SETPOINT_ADC    400.0f   // update this with your S value!
#define KP_DEFAULT        0.1367f  // Auto-tuned!
#define KI_DEFAULT        0.0f
#define KD_DEFAULT        0.0002f  // Auto-tuned!

// ── Feed-forward bias ─────────────────────────────────────────────────────────
// Equilibrium PWM duty measured in coil_test.ino (the duty where the coil
// just starts attracting the ball). PID output is added to this so the coil
// immediately starts near the working point on arm instead of winding from 0.
#define PWM_BIAS         265.0f   // from coil test: reaction range 251-281

// ── Safety thresholds (raw ADC) ───────────────────────────────────────────────
// Ball too close / touching core → ADC drops very low (field shunted through ball)
#define CONTACT_ADC_MIN   50    // ADC below this = ball on core face → cut PWM
// Ball fallen away             → cleanSignal returns to baseline (~455)
#define FALL_ADC_MAX      650   // Wide limit so it doesn't false-abort

// ── Serial logging ────────────────────────────────────────────────────────────
#define SERIAL_BAUD    115200UL
#define LOG_EVERY_N       25     // flush CSV every 25 cycles (~40 Hz)

// ════════════════════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ════════════════════════════════════════════════════════════════════════════

float    g_kp = KP_DEFAULT, g_ki = KI_DEFAULT, g_kd = KD_DEFAULT;
float    g_integral      = 0.0f;
float    g_prevMeas      = 0.0f;
float    g_filteredDeriv = 0.0f;
bool     g_firstRun      = true;
const float DERIV_ALPHA  = 0.30f;  // higher = faster derivative response (was 0.15)

float    g_setpoint   = SETPOINT_ADC;
bool     g_running    = false;
uint16_t g_lastDuty   = 0;
uint32_t g_lastLoopUs = 0;
uint16_t g_ledCounter = 0;
uint16_t g_logCounter = 0;
float    g_logSensor  = 0, g_logErr = 0, g_logOut = 0;

static char    g_cmdBuf[24];
static uint8_t g_cmdIdx = 0;

bool  g_manualMode = false;
float g_manualPWM  = 0.0f;

// ════════════════════════════════════════════════════════════════════════════
//  INTERFERENCE CANCELLER
// ════════════════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════════════════
//  HALL SENSOR
// ════════════════════════════════════════════════════════════════════════════
void hallBegin() {
  pinMode(HALL_PIN, INPUT);
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
  ADCSRA = (ADCSRA & 0xF8) | 0x04;   // ADC prescaler 16 -> 1 MHz clock
#endif
  for (uint8_t i = 0; i < 16; i++) { analogRead(HALL_PIN); delayMicroseconds(50); }
}

float hallReadRaw() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < HALL_OVERSAMPLE; i++) sum += (uint16_t)analogRead(HALL_PIN);
  return (float)sum / (float)HALL_OVERSAMPLE;
}

// ════════════════════════════════════════════════════════════════════════════
//  PWM DRIVER  (Timer1, pin 9, 31 kHz, 9-bit)
// ════════════════════════════════════════════════════════════════════════════
void pwmBegin() {
  pinMode(PWM_PIN, OUTPUT);
  digitalWrite(PWM_PIN, LOW);
  TCCR1A = _BV(COM1A1) | _BV(WGM11);
  TCCR1B = _BV(WGM13)  | _BV(WGM12) | _BV(CS10);
  ICR1   = (uint16_t)PWM_MAX;
  OCR1A  = 0;
}

void pwmSet(uint16_t duty) {
  if (duty > (uint16_t)PWM_SOFT_LIMIT) duty = (uint16_t)PWM_SOFT_LIMIT;
  OCR1A      = duty;
  g_lastDuty = duty;
}

void pwmStop() {
  OCR1A      = 0;
  g_lastDuty = 0;
}

// ════════════════════════════════════════════════════════════════════════════
//  PID CONTROLLER
//
//  Control direction (confirmed by hardware test):
//    ADC rises above setpoint -> ball too close -> error < 0 -> output drops
//    ADC drops below setpoint -> ball too far   -> error > 0 -> output rises
//    => error = setpoint - measurement
// ════════════════════════════════════════════════════════════════════════════
float pidCompute(float measurement, float setpoint, float dt) {
  if (dt <= 0.0f) return 0.0f;

  // Control direction (confirmed by hardware observation):
  //   ADC too LOW  (~207) = ball too close  → error < 0 → output decreases → coil weaker ✓
  //   ADC too HIGH (~477) = ball too far    → error > 0 → output increases → coil stronger ✓
  float error = measurement - setpoint;

  // Proportional
  float pTerm = g_kp * error;

  // Integral — skip first run (dt may be large from setup overhead)
  if (!g_firstRun) {
    g_integral += error * dt;
    if (g_integral >  300.0f) g_integral =  300.0f;
    if (g_integral < -300.0f) g_integral = -300.0f;
  }
  float iTerm = g_ki * g_integral;

  // Derivative on measurement (avoids derivative kick on setpoint change)
  float dTerm = 0.0f;
  if (!g_firstRun) {
    float rawD = (measurement - g_prevMeas) / dt;
    g_filteredDeriv = DERIV_ALPHA * rawD + (1.0f - DERIV_ALPHA) * g_filteredDeriv;
    dTerm = -g_kd * g_filteredDeriv;
  }

  g_firstRun = false;
  g_prevMeas = measurement;
  return pTerm + iTerm + dTerm;
}

void pidReset() {
  g_integral      = 0.0f;
  g_prevMeas      = 0.0f;
  g_filteredDeriv = 0.0f;
  g_firstRun      = true;
}

// ════════════════════════════════════════════════════════════════════════════
//  SERIAL LOGGER & COMMAND PARSER
// ════════════════════════════════════════════════════════════════════════════
void loggerBegin() {
  Serial.begin(SERIAL_BAUD);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 3000UL)) {}

  Serial.println(F("# ────────────────────────────────────────────"));
  Serial.println(F("# Lavitator  --  Maglev Controller"));
  Serial.println(F("# Setpoint=346 ADC  Bias=265/480 PWM"));
  Serial.println(F("# Commands: P/I/D/S<val>  R=reset  M<val>=manual  A=auto"));
  Serial.println(F("# ────────────────────────────────────────────"));
  Serial.println(F("time_ms,sensor_raw,error,output,kp,ki,kd"));
}

void loggerAccept(float sensor, float err, float out) {
  g_logSensor = sensor; g_logErr = err; g_logOut = out;
  if (++g_logCounter >= LOG_EVERY_N) {
    g_logCounter = 0;
    Serial.print(millis());       Serial.print(',');
    Serial.print(g_logSensor, 1); Serial.print(',');
    Serial.print(g_logErr, 1);    Serial.print(',');
    Serial.print(g_logOut, 1);    Serial.print(',');
    Serial.print(g_kp, 4);        Serial.print(',');
    Serial.print(g_ki, 4);        Serial.print(',');
    Serial.println(g_kd, 4);
  }
}

bool processSerial(bool &outReset) {
  outReset = false;
  bool changed = false;
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (g_cmdIdx > 0) {
        g_cmdBuf[g_cmdIdx] = '\0';
        char  cmd = g_cmdBuf[0];
        float val = (g_cmdIdx > 1) ? atof(g_cmdBuf + 1) : 0.0f;
        switch (cmd) {
          case 'P': case 'p': g_kp = val; changed = true;
            Serial.print(F("# Kp=")); Serial.println(val, 4); break;
          case 'I': case 'i': g_ki = val; changed = true;
            Serial.print(F("# Ki=")); Serial.println(val, 4); break;
          case 'D': case 'd': g_kd = val; changed = true;
            Serial.print(F("# Kd=")); Serial.println(val, 4); break;
          case 'S': case 's': g_setpoint = val; changed = true;
            Serial.print(F("# SP=")); Serial.println(val, 1); break;
          case 'M': case 'm': g_manualPWM = val; g_manualMode = true; changed = true;
            Serial.print(F("# Manual PWM=")); Serial.println(val, 1); break;
          case 'A': case 'a': g_manualMode = false; outReset = true; changed = true;
            Serial.println(F("# Auto (PID) mode")); break;
          case 'R': case 'r': outReset = true; changed = true;
            Serial.println(F("# PID reset")); break;
          default:
            Serial.print(F("# Unknown: ")); Serial.println(cmd); break;
        }
        g_cmdIdx = 0;
      }
    } else {
      if (g_cmdIdx < (uint8_t)(sizeof(g_cmdBuf) - 1)) g_cmdBuf[g_cmdIdx++] = c;
    }
  }
  return changed;
}

// ════════════════════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ════════════════════════════════════════════════════════════════════════════
void setup() {
  loggerBegin();
  hallBegin();
  pwmBegin();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  g_lastLoopUs = micros();
  Serial.println(F("# Hold ball at levitation gap, then send 'R' to arm."));
}

void loop() {
  // 1. Enforce 1 kHz loop rate (non-blocking)
  uint32_t now     = micros();
  uint32_t elapsed = now - g_lastLoopUs;
  if (elapsed < LOOP_INTERVAL_US) return;

  float dt = (elapsed > 5000UL) ? 0.005f : (float)elapsed * 1.0e-6f;
  g_lastLoopUs = now;

  // 2. Read sensor and apply interference canceller
  float sensorRaw = hallReadRaw();
  float expected  = calcExpectedOffset(g_lastDuty);
  float cleanSignal = sensorRaw - expected + 455.0f;

  // 3. Safety checks — return early to block buffered 'R' from re-arming
  //    Contact: ball too close → cleanSignal drops very low
  if (cleanSignal < (float)CONTACT_ADC_MIN) {
    pwmStop(); g_running = false;
    Serial.println(F("# SAFETY: contact (ADC too low). Send 'R' to re-arm."));
    loggerAccept(cleanSignal, 0.0f, 0.0f);
    return;
  }
  //    Fall: ball gone → cleanSignal rises (above FALL_ADC_MAX)
  if (cleanSignal > (float)FALL_ADC_MAX) {
    pwmStop(); g_running = false;
    Serial.println(F("# SAFETY: ball fallen (ADC too high). Send 'R' to re-arm."));
    loggerAccept(cleanSignal, 0.0f, 0.0f);
    return;
  }

  // 4. Parse incoming serial commands
  bool doReset = false;
  processSerial(doReset);
  if (doReset) {
    pidReset();
    g_running = true;
    Serial.println(F("# Armed."));
  }

  // 5. Compute & apply output
  float output = 0.0f;
  if (g_running) {
    if (g_manualMode) {
      output = g_manualPWM;
    } else {
      // PID output + feed-forward bias so coil starts at equilibrium immediately
      output = pidCompute(cleanSignal, g_setpoint, dt) + PWM_BIAS;
    }
    int16_t duty = (int16_t)output;
    if (duty < 0)              duty = 0;
    if (duty > PWM_SOFT_LIMIT) duty = PWM_SOFT_LIMIT;
    pwmSet((uint16_t)duty);

    // LED heartbeat (~1 Hz)
    if (++g_ledCounter >= 1000) {
      g_ledCounter = 0;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  }

  // 6. Log telemetry
  loggerAccept(cleanSignal, g_setpoint - cleanSignal, output);
}
