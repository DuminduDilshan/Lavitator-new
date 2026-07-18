# Lavitator — Wiring Guide & Pin Connections

---

## Complete Wiring Diagram

```
  ╔════════════════════════════════════════════════════════════════════╗
  ║                    12V DC POWER SUPPLY (≥5A)                       ║
  ║                     (+)             (-)                             ║
  ╚══════════╤══════════════════════════╤═════════════════════════════╝
             │                          │
             │                          └────────────────── GND (common star point) ◄──────────┐
             │                                                                                   │
             │ +12V rail                                                                         │
             ├────────────────────────── [2A FUSE] ─────────────────────────────────────┐       │
             │                                                                           │       │
             │                                                            ┌──── Coil (+) ┘       │
             │                                     [SB560 Flyback Diode]  │                      │
             │                                     Cathode ──►|── Anode   │  820-turn C-Core     │
             │                                         │              │   │  Coil                │
             │ ────────────────────────────────────────┘              └── Coil (-)               │
             │                                                             │                      │
             │                                                    IRF3205 DRAIN                  │
             │                                                             │                      │
             │                                                    IRF3205 SOURCE ────────────────┤
             │                                                             │                      │
             │                                              ┌──────────── IRF3205 GATE            │
             │                                              │                                     │
             │                                          [150Ω gate resistor]                      │
             │                                              │                                     │
             │                                      Arduino Nano D9 (PWM)                        │
             │                                                                                    │
             └───────────────────────────────────────────────────────────────────────────────────┘  │
  SS49E Hall Sensor (flat face toward core air gap)
    Pin 1 VCC  ── [100µF + 100nF caps to GND] ── Arduino Nano 5V pin
    Pin 2 GND  ── GND (common star point)
    Pin 3 OUT  ── [1kΩ] ──┬── Arduino A0
                          │
                        [100nF]
                          │
                         GND (star point)
```

---

## Component Pin Connections — Quick Reference

### Arduino Nano

| Pin | Wire To | Colour Suggestion | Notes |
|-----|---------|-------------------|-------|
| `D9` | 150Ω resistor → IRF3205 Gate | Yellow | Timer1 OC1A — 31 kHz PWM |
| `A0` | 1kΩ RC filter output | Green | Hall sensor analog input |
| `GND` | Common star-point GND | Black | Must share with PSU GND |
| `5V` | *(leave unconnected for sensor)* | — | Do NOT power SS49E from here |
| `VIN` / `USB` | 5V USB or regulated 5V supply | Red | Arduino logic supply |

---

### IRF3205 MOSFET — TO-220 Pinout

```
  Flat (label) face toward you:

  ┌─────────────────┐
  │   IRF3205       │
  │                 │
  └──┬──────┬───┬──┘
     │      │   │
     G      D   S
  (Gate) (Drain) (Source)
```

| Pin | Wire To | Notes |
|-----|---------|-------|
| **Gate (G)** — left | Arduino `D9` via 150Ω series resistor + 10kΩ pull-down to GND | Pull-down keeps gate at 0V when Arduino is off |
| **Drain (D)** — middle | Coil (−) terminal + SB560 Anode | Switched side of load |
| **Source (S)** — right | GND (star point) | Keep this wire short and thick |

### ⚠️ Gate Pull-Down Resistor — REQUIRED

```
Arduino D9 ──[150Ω]──┬──── IRF3205 Gate
                      │
                    [10kΩ]   ← MUST ADD — prevents floating gate
                      │
                     GND
```

**Without the 10kΩ pull-down**, the gate floats to an unknown voltage when
the Arduino is off, unplugged, or resetting. A floating gate can partially or
fully turn the MOSFET ON, causing it to overheat even with no load connected.
This is the most common wiring mistake and the #1 cause of unexpected MOSFET heating.

> R_DS(on) = 8 mΩ → at 3.5A dissipates only **~0.1W**. Heatsink not required,
> but clip one on if running at high duty for extended test sessions.

---

### SB560 Flyback Diode — DO-201

```
  Anode ──────►|──── Cathode
                      (silver band)
```

| Terminal | Wire To | Notes |
|----------|---------|-------|
| **Cathode** (silver band) | +12V supply rail | As close to coil terminal as possible |
| **Anode** | Coil (−) terminal / IRF3205 Drain | Must be right at the drain node |

> ⚠️ **Install before first power-on.** The coil is highly inductive (~several mH).
> Without this diode, switch-off back-EMF spikes will destroy the IRF3205 instantly.

---

### SS49E Hall-Effect Sensor — TO-92 or SOT-89

```
  Flat face toward core pole face:

  ┌─────────┐
  │  SS49E  │  ← point flat face at core air gap
  └─┬──┬──┬─┘
    │  │  │
   VCC GND OUT
   (1) (2) (3)
```

| Pin | Wire To | Notes |
|-----|---------|-------|
| **Pin 1 VCC** | Arduino Nano `5V` pin | Add 100µF + 100nF decoupling caps right at VCC pin |
| **Pin 2 GND** | Common GND star point | |
| **Pin 3 OUT** | 1kΩ → Arduino `A0` (100nF to GND after resistor) | RC low-pass filter |

> ✅ **Arduino 5V pin is fine for the SS49E.** The sensor draws only ~6 mA
> and operates from 4.5–6V. The Arduino Nano's onboard regulator comfortably
> supplies this. Add a 100µF electrolytic + 100nF ceramic cap from the SS49E
> VCC pin to GND (placed physically close to the sensor) to suppress any
> switching transients from the PWM circuit.

> Mount the SS49E at the core pole face with the **flat face pointing directly
> into the air gap** where the ball will float. Adjust depth until the resting
> ADC (ball at ~15 mm) reads ≈ 512 on the Serial Monitor.

---

### RC Low-Pass Filter (build on breadboard near Arduino)

```
SS49E Pin 3 OUT ────[1 kΩ]────┬────── Arduino A0
                               │
                             [100 nF]
                               │
                              GND
```

| Component | Value | Purpose |
|-----------|-------|---------|
| Series resistor | 1 kΩ | With 100 nF forms RC filter |
| Shunt capacitor | 100 nF ceramic | f_c ≈ 1.6 kHz; kills 31 kHz PWM noise |

---

### L7805 Sensor Power Regulator — TO-220 *(Optional upgrade)*

> **You don't need the L7805** for a working build. The Arduino 5V pin
> powers the SS49E fine with decoupling caps. The L7805 is only worth
> adding if you observe noisy ADC readings that the RC filter doesn't fix.

```
  Flat face toward you:

  ┌──────────┐
  │  L7805   │
  └─┬──┬──┬─┘
    │  │  │
   IN GND OUT
```

| Pin | Wire To |
|-----|--------|
| **IN** | +12V supply rail (after main fuse) |
| **GND** | Common GND star point |
| **OUT** | SS49E Pin 1 VCC; 100µF + 100nF caps (+ to OUT, - to GND) |

> Add 100µF electrolytic on both IN and OUT pins to prevent oscillation.

---

## Power Rails Summary

| Rail | Voltage | Source | Powers |
|------|---------|--------|--------|
| **+12V** | 12V DC | External PSU (≥5A) | Coil, MOSFET drain |
| **+5V** | 5V USB | PC / USB charger | Arduino Nano + SS49E VCC |
| **GND** | 0V | Star point | All components |
| *(+5V isolated)* | *(L7805 optional)* | *(from 12V rail)* | *(SS49E only — if noise is a problem)* |

---

## Wire Gauge Recommendation

| Connection | Wire Gauge | Reason |
|------------|------------|--------|
| 12V supply → Coil, Drain | **20 AWG minimum** | Up to 3.5A continuous |
| Source → GND star | **20 AWG minimum** | Same current return path |
| Gate resistor → Gate | 24–26 AWG | Signal only, µA |
| SS49E signal → A0 | 24–26 AWG | Analog signal, keep short |
| Arduino → GND | 24 AWG | Low current |

---

## 🔥 Coil Thermal Safety

The coil is **3.4Ω**. Power dissipated = V²/R = I² × R.

### Direct DC (no PWM) — for reference only, never run this way at 12V:

| Voltage | Current | Coil Power | Result |
|---------|---------|-----------|--------|
| 3V | 0.88A | 2.3W | Warm — brief test OK |
| 5V | 1.47A | 7.3W | Hot — seconds only |
| 6V | 1.76A | 10.6W | Very hot — enamel risk |
| **12V DC** | **3.53A** | **42W** | 🔥 Destroys coil in minutes |

### With PWM + PID control (normal operation):

At levitation equilibrium, the ball needs only ~0.4A average current:
- Coil power ≈ 0.4² × 3.4 ≈ **0.5W** — barely warm
- Supply voltage does **not** determine coil heat — the PID duty cycle does

### Auto-cutoff (built into firmware):

If the ball falls beyond 45mm, the firmware immediately cuts PWM to zero
(`FALL_GAP_MAX_MM` in `config.h`) preventing runaway coil heating.

> ⚠️ **If the coil is too hot to touch during testing**, the ball has fallen and PWM
> has saturated at full duty. Cut 12V power immediately, re-arm with `R` command
> once the ball is repositioned.

---

## Safety Checklist Before First Power-On

- [ ] SB560 flyback diode installed, polarity correct (cathode/band → +12V)
- [ ] 2A fast-blow fuse in +12V supply line
- [ ] 150Ω gate resistor between Arduino D9 and IRF3205 Gate
- [ ] RC filter (1kΩ + 100nF) on SS49E signal line
- [ ] SS49E VCC connected to Arduino 5V pin with 100µF + 100nF decoupling caps
- [ ] All GNDs connected to one star point
- [ ] Coil resistance checked: ≈ 3–4Ω with multimeter (open = broken wire, <1Ω = shorted turns)
- [ ] Arduino firmware uploaded, Serial Monitor shows CSV telemetry at 115200 baud
- [ ] Catch-net or foam pad placed below ball levitation zone
- [ ] **Never apply 12V supply without firmware running** — uncontrolled full duty will overheat coil

---

*Last updated: 2026-07-03*
