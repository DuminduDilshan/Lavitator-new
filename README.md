# 🔮 Lavitator — C-Core Magnetic Levitation System

A closed-loop electromagnetic levitation (EML) system that suspends a
ferromagnetic steel ball in mid-air using a salvaged transformer C-core
electromagnet, controlled by a PID algorithm running at 1 kHz on an
Arduino Nano.

---

## Project Structure

```
Lavitator/
├── firmware/
│   └── maglev_controller/
│       ├── maglev_controller.ino   ← Main sketch (open this in Arduino IDE)
│       ├── config.h                ← All tunable constants — start here
│       ├── pid.h / pid.cpp         ← PID controller class
│       ├── hall_sensor.h / .cpp    ← SS49E Hall sensor + calibration
│       ├── pwm_driver.h / .cpp     ← Timer1 31 kHz PWM abstraction
│       └── serial_logger.h / .cpp  ← Buffered CSV logger + command parser
│
├── hardware/
│   ├── bom.md                      ← Bill of Materials (~$30–65 total)
│   └── wiring_guide.md             ← Wiring diagram, safety notes, checklist
│
├── simulation/
│   ├── maglev_sim.py               ← Physics simulation (Euler integration)
│   ├── plot_response.py            ← Response plotter (dark-themed, 4 panels)
│   └── requirements.txt
│
├── tools/
│   ├── tuning_dashboard.py         ← Real-time serial plotter + gain sliders
│   └── requirements.txt
│
└── maglev-ccore-design-notes.md   ← Original design notes & physics estimates
```

---

## How It Works

```
Hall Sensor (A0) → PID Controller → Timer1 PWM (D9) → IRFZ44N → C-Core Coil
      ↑                                                                  ↓
  Steel ball position ←────────────── Magnetic attractive force ←───────┘
```

- The system is **open-loop unstable** (Earnshaw's theorem): without the
  controller, the ball either slams into the core or falls.
- The **derivative term** does most of the stabilizing work.
- The **1 kHz control loop** is enforced using `micros()` (not `delay()`).
- **31 kHz PWM** (Timer1) prevents audible coil whine and gives fine
  current resolution (9-bit effective, 0–511 duty).

### Key Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Core cross-section | 7 cm² | 3.5 × 2 cm salvaged transformer |
| Working gap | 15–20 mm | Force ∝ 1/g², tighter = more control authority |
| Coil | 820 turns, 22 AWG | ~3.4 Ω, ~2870 A·turns at 12V |
| Ball mass | 10–30 g | 20 g is a good starting point |
| Control rate | 1 kHz | 1 ms loop interval |
| PWM frequency | ~31.25 kHz | Timer1, ICR1=511, no prescaler |
| Setpoint | 512 ADC | Calibrate live; ≈ 15 mm gap |

---

## Quickstart

### 1. Hardware

See [`hardware/wiring_guide.md`](hardware/wiring_guide.md) for the full
wiring diagram and safety checklist.

**Minimum connections:**
```
Arduino D9 → 150Ω → IRF3205 gate
Arduino A0 ← SS49E signal (via 1kΩ + 100nF RC filter)
SB560 diode across coil (cathode to +12V)
SS49E powered from isolated +5V rail
```

### 2. Firmware

1. Open `firmware/maglev_controller/maglev_controller.ino` in Arduino IDE 2.x
2. Select **Arduino Nano** and correct COM port
3. Click **Upload**
4. Open Serial Monitor at **115200 baud** — you should see CSV telemetry

> If the ball drops immediately, that's expected! It will during initial tuning.

### 3. Calibrate the Hall Sensor

The default setpoint is 512 ADC (~15 mm gap). To calibrate:

1. Hold the ball at ~15 mm from the core face with no power
2. Note the `sensor_raw` value in Serial Monitor
3. Update `SETPOINT_ADC` in `config.h` to that value, or
4. Send `S<value>\n` over serial to set it live (e.g. `S485\n`)

### 4. First Levitation

Start with `Kd=1.8, Kp=2.5, Ki=0.0` (firmware defaults). Hold the ball at
~15 mm and turn on power. The controller will attempt to hold position.

**Tuning procedure:**
1. Set `Ki=0, Kd=0`
2. Raise `Kp` until oscillation begins, then reduce by ~35%
3. Raise `Kd` until oscillation is damped
4. Add small `Ki` (0.05–0.1) only if needed to eliminate steady-state offset

---

## Live Tuning Dashboard

```bash
cd tools
pip install -r requirements.txt
python tuning_dashboard.py          # auto-detect COM port
python tuning_dashboard.py COM3     # specify port
python tuning_dashboard.py --sim    # simulation mode (no hardware needed)
```

Features: live gap/error/duty plots, Kp/Ki/Kd/setpoint sliders, Reset button,
auto COM port detection, CSV session logging.

---

## Physics Simulation

Validate your gains before flashing:

```bash
cd simulation
pip install -r requirements.txt

# Run with default gains
python maglev_sim.py

# Custom gains
python maglev_sim.py --kp 3.0 --kd 2.5 --duration 3.0

# Kd gain sweep (find stable region)
python maglev_sim.py --sweep

# Visualize results
python plot_response.py sim_output.csv
```

---

## Serial Command Reference

Connect any serial terminal at 115200 baud and send (newline-terminated):

| Command | Example | Effect |
|---------|---------|--------|
| `P<val>` | `P2.5` | Set Kp = 2.5 |
| `I<val>` | `I0.05` | Set Ki = 0.05 |
| `D<val>` | `D1.8` | Set Kd = 1.8 |
| `S<val>` | `S512` | Set setpoint to 512 ADC |
| `R` | `R` | Soft reset (wipes PID state, re-arms) |

CSV output format (40 Hz):
```
time_ms, sensor_raw, gap_mm, error, output, kp, ki, kd
```

---

## Known Issues & Tips

- **Ball oscillates and drops**: Normal during initial tuning. Kd is too low or
  Kp is too high. Reduce Kp first.
- **Serial log slows the loop**: Firmware buffers and flushes every 25 cycles
  (configurable via `LOG_EVERY_N` in `config.h`).
- **Core saturation**: Estimated flux density ≈ 0.24 T at 15 mm gap — well
  below silicon-steel saturation (~1.5 T). There is headroom to increase current.
- **Hall noise**: If sensor reading is noisy, check the RC filter and that the
  sensor supply is isolated from the 12V/PWM rail.
- **Thermal**: Monitor MOSFET temperature at duty > 60%. Add a heatsink.

---

## License

MIT — free to use, modify, and share with attribution.

---

*Built with: Arduino Nano, IRF3205, SS49E, Python, and a salvaged transformer.*
