# Lavitator — Bill of Materials

## Electromagnet & Core

| # | Part | Qty | Spec / Notes | Source |
|---|------|-----|-------------|--------|
| 1 | Transformer C-core | 1 | Salvaged from mains transformer. Cross-section ≥ 5 cm² preferred. Lavitator uses 3.5 cm × 2 cm = 7 cm² | Salvaged / eBay |
| 2 | Enameled copper wire, 22 AWG | ~50 m | Magnet wire (polyimide / enamel insulation). 820 turns → ~45 m + waste | Amazon / AliExpress |
| 3 | Bobbin / coil former | 1 | 3D-print or wind on cardboard former that fits the C-core leg | DIY |

## Driver Circuit

| # | Part | Qty | Spec / Notes | Source |
|---|------|-----|-------------|--------|
| 4 | MOSFET — IRF3205 | 1 | N-channel, 55V / **110A**, TO-220. R_DS(on) = 8 mΩ — dissipates only ~0.1W at 3.5A coil current. V_GS(th) = 2–4V; Arduino 5V logic drives it to full enhancement. Same G-D-S pinout as IRFZ44N (drop-in) | Mouser / DigiKey / AliExpress |
| 5 | Flyback diode — SB560 | 1 | Schottky, 5A / 60V, DO-201. Place directly across coil terminals, cathode to +12V rail. Prevents MOSFET destruction on switch-off | Mouser / DigiKey |
| 6 | Gate resistor | 1 | 100–220 Ω, ¼W. Limits gate charge current and reduces EMI ringing | Any |
| 7 | Current-sense resistor *(optional)* | 1 | 0.1–0.5 Ω, 2–5W, low-inductance. Place in MOSFET source path for inner current-limit loop. Connect to Arduino A1 via voltage divider. Not used in baseline firmware | Mouser |

## Sensing

| # | Part | Qty | Spec / Notes | Source |
|---|------|-----|-------------|--------|
| 8 | Hall-effect sensor — SS49E | 1 | Linear output, ±1000 Gauss range, 5V powered. Mount at core pole face, signal wire to Arduino A0 | AliExpress / Mouser |
| 9 | RC filter resistor | 1 | 1 kΩ, ¼W, 5% — in series with SS49E signal line | Any |
| 10 | RC filter capacitor | 1 | 100 nF ceramic (0.1 µF), 50V — from sensor signal to GND, after series resistor. Suppresses PWM switching noise on analog read | Any |

## Controller & Power

| # | Part | Qty | Spec / Notes | Source |
|---|------|-----|-------------|--------|
| 11 | Arduino Nano | 1 | ATmega328P @ 16 MHz, 5V logic. Genuine or clone (CH340 USB-serial). Nano Every NOT recommended (different timer registers) | Amazon / AliExpress |
| 12 | 12V DC power supply | 1 | ≥5A current rating (coil draws up to 3.5A peak). Switching PSU or bench supply | Amazon |
| 13 | 5V linear regulator — L7805 or LM317 | 1 | Provides clean isolated 5V rail for Hall sensor only. Alternatively use a separate 5V USB power bank. Keep sensor supply separate from 12V/PWM supply to reduce noise | Any |
| 14 | Smoothing capacitor, 12V rail | 1 | 470–1000 µF, 25V electrolytic — near MOSFET drain / coil supply terminal | Any |
| 15 | Smoothing capacitor, 5V rail | 1 | 100 µF, 16V electrolytic — on sensor VCC rail | Any |
| 16 | Decoupling capacitor | 2 | 100 nF ceramic — on Arduino 5V and sensor 5V supply pins, near chips | Any |

## Mechanical

| # | Part | Qty | Spec / Notes | Source |
|---|------|-----|-------------|--------|
| 17 | Ferromagnetic steel ball | 1–3 | 10–30 g, smooth surface. ⌀ 25 mm (~65 g) works well at 15 mm gap with this core. Start lighter and work up | Amazon / bearing supplier |
| 18 | Catch net / foam pad | 1 | Placed below ball during initial tuning to prevent floor damage when ball drops | DIY |
| 19 | Core mounting frame | 1 | 3D-printed or aluminium extrusion. Must hold core rigidly; vibration causes Hall noise | DIY |
| 20 | Hall sensor mount | 1 | 3D-printed bracket holding SS49E at pole face, adjustable depth | DIY |

## Prototyping

| # | Part | Qty | Spec / Notes |
|---|------|-----|-------------|
| 21 | Breadboard (full-size) | 1 | For driver circuit during development |
| 22 | Jumper wires | assorted | Male–male and male–female |
| 23 | Heatsink + M3 screw | 1 set | Clip-on or bolted TO-220 heatsink for IRFZ44N — MOSFET can get warm at 3A+ continuous |

## Estimated Total Cost

| Category | Approximate Cost (USD) |
|----------|----------------------|
| Core + wire | $5–15 (salvaged core) |
| MOSFET + diode + resistors | $3–5 |
| Hall sensor | $1–3 |
| Arduino Nano clone | $4–8 |
| Power supply (12V 5A) | $10–20 |
| Steel ball | $2–5 |
| Misc (caps, wire, PCB) | $5–10 |
| **Total** | **~$30–65** |

---

*Last updated: 2026-07-03*
