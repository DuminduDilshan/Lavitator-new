# Magnetic Levitation System — C-Core Electromagnet Design Notes

## Description

Design notes for an electromagnetic levitation (EML) system using a
repurposed transformer C-core. A ferromagnetic steel ball is levitated in
the air gap of the core via closed-loop PID control, using Hall-effect
position sensing and PWM current control.

## Hardware / Setup

**Core**
- Salvaged transformer C-core, cross-sectional area **3.5 cm × 2 cm = 7 cm²**
- Original air gap in sketch: 40 mm — recommended working gap: **15–20 mm**
  (force scales ~1/g², so a tighter gap gives much stronger, more
  controllable force with the same ampere-turns)

**Coil**
- 820 turns, wound with new **22 AWG enameled copper wire**
- Estimated resistance: ~3.4 Ω
- At 12 V supply → ~3.5 A → NI ≈ 2870 ampere-turns

**Levitated object**
- Small ferromagnetic steel ball (10–30 g recommended for initial testing)

**Sensing**
- SS49E Hall-effect sensor, mounted at/near the pole face
- Powered from a separate, regulated 5 V rail (isolated from PWM/coil
  supply to avoid switching noise coupling into the analog reading)
- RC low-pass filter on sensor output (1 kΩ + 100 nF) to suppress PWM noise

**Driver**
- IRFZ44N MOSFET (or IRLZ44N if driving from 3.3V logic — not needed with
  Arduino Nano's 5V logic)
- Flyback diode (SB560 / 1N5822) across the coil, cathode to supply rail
- Gate resistor ~100–220 Ω
- Optional current-sense resistor (0.1–0.5 Ω) for inner current-limit loop

**Controller**
- Arduino Nano
- Coil PWM driven from Timer1 (pins 9/10) reconfigured for ~31 kHz fast
  PWM — default `analogWrite()` frequency (~490 Hz–980 Hz) is too low and
  causes audible whine + coarse current control

## Force / Field Estimates

Using F ≈ μ₀N²I²A / (2g²), with N = 820, I = 3.5 A, A = 7×10⁻⁴ m²:

| Gap (g) | Estimated Force | Notes |
|---------|-----------------|-------|
| 15 mm   | ~16 N           | Large margin over ball weight — good control authority |
| 40 mm   | ~2.3 N          | Workable but much lower control gradient (dF/dx) |

Core flux density check at g = 15mm: B ≈ μ₀NI/g ≈ **0.24 T** — well below
typical silicon-steel saturation (1.5–1.8 T), so there's headroom to
increase current later if needed.

## Control Loop

Position (Hall sensor) → PID → PWM duty → coil current → magnetic force →
ball position (closes the loop).

- System is **open-loop unstable** (Earnshaw's theorem) — attractive-only
  force means the derivative term does most of the stabilizing work.
- Sign convention: ball too far → increase current; ball too close →
  cut current sharply (unstable direction).
- Target loop rate: **≥1 kHz** (Nano's 16 MHz clock handles this fine).
- Starting gains: moderate Kp (tune up to onset of oscillation, back off
  ~30–40%), Kd dominant, Ki small/zero initially (integral windup risk
  near pole contact).

### Arduino Nano PID skeleton

```cpp
const int HALL_PIN = A0;
const float SETPOINT = 512;      // 10-bit ADC, calibrate live to ~15-20mm gap
float Kp = 2.5, Ki = 0.0, Kd = 1.8;   // starting point, requires live tuning
float integral = 0, lastError = 0;
unsigned long lastTime = 0;

void setup() {
  // Timer1 fast PWM, no prescaler -> ~31kHz on pins 9 & 10
  TCCR1A = _BV(COM1A1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
  ICR1 = 511;
}

void loop() {
  unsigned long now = micros();
  float dt = (now - lastTime) / 1e6;
  if (dt < 0.001) return;   // enforce ~1kHz loop
  lastTime = now;

  int sensorVal = analogRead(HALL_PIN);
  float error = SETPOINT - sensorVal;

  integral += error * dt;
  integral = constrain(integral, -300, 300);  // anti-windup clamp

  float derivative = (error - lastError) / dt;
  lastError = error;

  float output = Kp*error + Ki*integral + Kd*derivative;
  OCR1A = constrain((int)output, 0, 511);
}
```

## Known Issues / Open Points

- PID gains are starting estimates only — require live tuning; expect the
  ball to drop/oscillate repeatedly during initial tuning (normal for EML).
- Physical catch-net or stop recommended under the ball during testing.
- Data logging at 1kHz loop rate via `Serial.println` will slow the loop —
  buffer values and dump every 20–50 cycles, or log to SD, if continuous
  high-rate data is needed for report plots.
- Core saturation and thermal limits not yet verified experimentally —
  estimates only, based on typical silicon-steel transformer core specs.

## Comparison: Levitated Object Choice

| Option | Verdict | Reason |
|---|---|---|
| Ferromagnetic steel ball | **Chosen** | Matches C-core attraction physics, easy sensing, well-documented in EML literature |
| Permanent magnet | Rejected | Needs attract/repel control, harder with single coil |
| Diamagnetic (graphite) | Rejected | Requires field strength far beyond this core's capability |
| Second electromagnet | Rejected | Doubles instability/control complexity, unnecessary here |
