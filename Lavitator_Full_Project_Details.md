# 🔮 Lavitator — Full Project Details

A comprehensive guide and reference document for the Lavitator C-Core Magnetic Levitation System.

---

## 1. Project Overview

Lavitator is a closed-loop electromagnetic levitation (EML) system that suspends a ferromagnetic steel ball in mid-air using a salvaged transformer C-core electromagnet. It is controlled by a PID algorithm running at 1 kHz on an Arduino Nano.

- **System Type**: Closed-loop electromagnetic levitation
- **Controller**: Arduino Nano
- **Control Rate**: 1 kHz (enforced via `micros()`)
- **PWM Frequency**: 31.25 kHz (Timer1) to prevent audible coil whine
- **Sensor**: SS49E Hall-effect sensor
- **Driver**: IRF3205 MOSFET

The system is open-loop unstable (due to Earnshaw's theorem), meaning the derivative term of the PID controller does most of the stabilizing work.

---

## 2. Hardware & Design

### Core and Coil
- **Core**: Salvaged transformer C-core, cross-sectional area 3.5 cm × 2 cm = 7 cm²
- **Working Gap**: 15–20 mm (provides a much stronger, more controllable force gradient compared to a wider gap)
- **Coil**: 820 turns, wound with 22 AWG enameled copper wire
- **Resistance**: ~3.4 Ω
- **Current**: At 12 V supply → ~3.5 A → NI ≈ 2870 ampere-turns
- **Force Estimate**: At a 15 mm gap, the estimated force is ~16 N, offering a large margin over the weight of a typical 10–30 g steel ball.
- **Flux Density**: Estimated at B ≈ 0.24 T at a 15 mm gap (well below the saturation point of silicon-steel).

### Levitated Object
- Ferromagnetic steel ball (10–30 g recommended for initial testing).
- Matches C-core attraction physics and allows easy position sensing.

### Controller & Driver Configuration
- The coil PWM is driven from Timer1 (pins 9/10) reconfigured for ~31 kHz fast PWM. The default `analogWrite()` frequency (~490 Hz–980 Hz) causes audible whine and provides coarse current control.
- **IRF3205 MOSFET**: Handles the coil switching. Its very low on-resistance (~8 mΩ) means it dissipates only ~0.1W at 3.5A, so a heatsink is usually unnecessary under normal operation.

---

## 3. Wiring Guide

### Power Rails
- **+12V**: External PSU (≥5A) for the Coil and MOSFET drain.
- **+5V**: 5V USB for Arduino Nano and SS49E VCC.
- **GND**: Common star point for all components.

### Arduino Nano Connections
| Pin | Wire To | Notes |
|-----|---------|-------|
| `D9` | 150Ω resistor → IRF3205 Gate | Timer1 OC1A — 31 kHz PWM |
| `A0` | 1kΩ RC filter output | Hall sensor analog input |
| `GND` | Common star-point GND | Must share with PSU GND |

### MOSFET (IRF3205) Connections
- **Gate**: Arduino `D9` via 150Ω series resistor. **Requires a 10kΩ pull-down to GND** to prevent floating gate states.
- **Drain**: Coil (−) terminal and SB560 Anode.
- **Source**: GND (star point).

### Important Safety Components
- **SB560 Flyback Diode**: Must be installed across the coil (Cathode to +12V, Anode to Drain). Prevents back-EMF spikes from destroying the MOSFET.
- **Gate Pull-Down**: A 10kΩ resistor from Gate to GND prevents the MOSFET from turning on when the Arduino is off.
- **Hall Sensor RC Filter**: A 1 kΩ series resistor and 100 nF shunt capacitor to GND on the sensor output line kills 31 kHz PWM noise.
- **Sensor Decoupling**: Add 100µF + 100nF decoupling capacitors right at the SS49E VCC pin.

---

## 4. Firmware Configuration

The controller settings are centralized in `firmware/maglev_controller/config.h`.

### Key Parameters
- **Loop Interval**: `1000 µs` (1 kHz loop)
- **Target Levitation Gap (Setpoint)**: Default is `512.0` (10-bit ADC target). Must be calibrated live by holding the ball at ~15 mm.
- **PWM Constraints**: `PWM_MAX = 511`, `PWM_SOFT_LIMIT = 480` (~94% max duty, reserves headroom).
- **Default PID Gains**:
  - `Kp = 2.5`
  - `Ki = 0.0`
  - `Kd = 1.8`
- **Safety Thresholds**:
  - `CONTACT_ADC_MIN = 40`: Controller cuts PWM if the ball contacts the core.
  - `FALL_GAP_MAX_MM = 45.0`: Controller cuts PWM if the ball falls.

### Tuning Procedure
1. Set `Ki=0, Kd=0`.
2. Raise `Kp` until oscillation begins, then reduce by ~35%.
3. Raise `Kd` until oscillation is damped.
4. Add small `Ki` (0.05–0.1) only if needed to eliminate steady-state offset.

---

## 5. Software & Tooling

### Serial Commands
Communicate at 115200 baud (newline-terminated):
- `P<val>`: Set Kp (e.g., `P2.5`)
- `I<val>`: Set Ki (e.g., `I0.05`)
- `D<val>`: Set Kd (e.g., `D1.8`)
- `S<val>`: Set setpoint to ADC value (e.g., `S512`)
- `R`: Soft reset (wipes PID state, re-arms)

### Included Python Tools
- **Live Tuning Dashboard** (`tools/tuning_dashboard.py`): Real-time serial plotter, gain sliders, and CSV session logging.
- **Physics Simulation** (`simulation/maglev_sim.py`): Validates PID gains using an Euler integration simulation before testing on hardware.
- **Response Plotter** (`simulation/plot_response.py`): Generates dark-themed, 4-panel plots from simulation outputs.

---

*Compiled from Lavitator Project Documentation (README, Design Notes, Wiring Guide, and Firmware Configuration).*
