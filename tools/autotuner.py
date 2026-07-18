"""
autotuner.py — Lavitator PID Auto-Tuner Companion
Connects to the autotuner.ino firmware via serial, plots the relay
oscillation in real-time, and displays the computed gains.

Usage:
    python tools/autotuner.py          # auto-detect port
    python tools/autotuner.py COM5     # specify port
"""

import sys
import time
import threading
import queue
import re
import serial
import serial.tools.list_ports
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# ── Config ────────────────────────────────────────────────────────────────────
BAUD         = 115200
WINDOW_S     = 12.0      # seconds of history shown
SETPOINT_ADC = 346.0     # your levitation ADC
RELAY_AMP    = 50.0      # must match firmware RELAY_AMP
MAX_POINTS   = 5000
# ──────────────────────────────────────────────────────────────────────────────

# Colour palette
BG      = '#0d1117'
AX_BG   = '#161b22'
C_SEN   = '#58a6ff'
C_OUT   = '#7ee787'
C_SP    = '#f78166'
C_GRID  = '#21262d'
C_TEXT  = '#c9d1d9'
C_TITLE = '#e6edf3'

STATE_LABELS = {0: 'IDLE', 1: 'SETTLING', 2: 'RELAY', 3: 'DONE'}
STATE_COLORS = {0: '#555', 1: '#ffa657', 2: '#58a6ff', 3: '#7ee787'}

data_queue   = queue.Queue()
results      = {}          # filled when tuning completes
t_data       = []
sensor_data  = []
output_data  = []
state_data   = []
t0_wall      = time.time()


# ── Serial detection ──────────────────────────────────────────────────────────
def detect_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if 'CH340' in (p.description or '') or 'Arduino' in (p.description or ''):
            return p.device
    if ports:
        return ports[0].device
    return None


def find_port(argv):
    if len(argv) > 1:
        return argv[1]
    p = detect_port()
    if p:
        print(f'[autotuner] Auto-detected port: {p}')
        return p
    raise RuntimeError('No serial port found. Pass port as argument: python autotuner.py COM5')


# ── Serial reader thread ──────────────────────────────────────────────────────
def serial_reader(ser):
    """Read from an already-open serial port and push data to the queue."""
    time.sleep(2.0)
    ser.reset_input_buffer()
    print('[autotuner] Connected -- send  T  in Serial Monitor to start tuning.')

    while True:
        try:
            raw = ser.readline().decode(errors='replace').strip()
        except Exception:
            break
        if not raw:
            continue

        print(f'  {raw}')

        # Parse comment lines for results
        if raw.startswith('# RESULT'):
            m_kp = re.search(r'Kp=([-\d.]+)', raw)
            m_kd = re.search(r'Kd=([-\d.]+)', raw)
            if m_kp and m_kd:
                results['Kp'] = float(m_kp.group(1))
                results['Kd'] = float(m_kd.group(1))

        # Parse CSV: time_ms,sensor_raw,output,state
        if raw and raw[0].isdigit():
            parts = raw.split(',')
            if len(parts) == 4:
                try:
                    ts      = float(parts[0]) / 1000.0
                    sensor  = float(parts[1])
                    out_val = float(parts[2])
                    state   = int(parts[3])
                    data_queue.put((ts, sensor, out_val, state))
                except ValueError:
                    pass


# ── Send command helper ───────────────────────────────────────────────────────
_ser_lock  = threading.Lock()
_serial_h  = None

def send_cmd(cmd: str):
    global _serial_h
    if _serial_h and _serial_h.is_open:
        with _ser_lock:
            _serial_h.write((cmd + '\n').encode())


# ── Plot setup ────────────────────────────────────────────────────────────────
def build_figure():
    fig, (ax_sen, ax_out) = plt.subplots(2, 1, figsize=(12, 7),
                                          facecolor=BG, sharex=True)
    fig.canvas.manager.set_window_title('Lavitator — PID Auto-Tuner')
    fig.suptitle('Relay Auto-Tune  |  Send  T  in Serial Monitor to start',
                 color=C_TITLE, fontsize=13, fontweight='bold')

    for ax in (ax_sen, ax_out):
        ax.set_facecolor(AX_BG)
        ax.tick_params(colors=C_TEXT)
        ax.spines[:].set_color(C_GRID)
        ax.yaxis.label.set_color(C_TEXT)
        ax.grid(color=C_GRID, linewidth=0.5)

    # Sensor axis
    ax_sen.set_ylabel('Sensor ADC')
    ax_sen.set_ylim(100, 550)
    line_sen,  = ax_sen.plot([], [], color=C_SEN,  lw=1.5, label='Sensor raw')
    line_sp    = ax_sen.axhline(SETPOINT_ADC, color=C_SP, lw=1.2,
                                linestyle='--', label=f'Setpoint {int(SETPOINT_ADC)}')
    ax_sen.legend(loc='upper right', facecolor=AX_BG, labelcolor=C_TEXT)

    # Output (duty) axis
    ax_out.set_ylabel('PWM Duty (0–480)')
    ax_out.set_xlabel('Time (s)')
    ax_out.set_ylim(0, 500)
    line_out,  = ax_out.plot([], [], color=C_OUT,  lw=1.5, label='PWM output')
    high_line  = ax_out.axhline(265 + RELAY_AMP, color='#ffa657', lw=0.8,
                                linestyle=':', label=f'Relay HIGH={265+RELAY_AMP:.0f}')
    low_line   = ax_out.axhline(265 - RELAY_AMP, color='#ffa657', lw=0.8,
                                linestyle=':', label=f'Relay LOW={265-RELAY_AMP:.0f}')
    ax_out.legend(loc='upper right', facecolor=AX_BG, labelcolor=C_TEXT)

    # Result text box
    result_text = ax_sen.text(0.02, 0.95, '', transform=ax_sen.transAxes,
                               color='#7ee787', fontsize=11, fontweight='bold',
                               verticalalignment='top',
                               bbox=dict(facecolor='#0d2a0d', edgecolor='#7ee787',
                                         boxstyle='round,pad=0.4'))

    return fig, (ax_sen, ax_out), (line_sen, line_out), result_text


# ── Animation update ──────────────────────────────────────────────────────────
def make_updater(ax_sen, ax_out, line_sen, line_out, result_text):
    def update(_frame):
        # Drain queue
        while not data_queue.empty():
            ts, sensor, out_val, state = data_queue.get_nowait()
            t_data.append(ts)
            sensor_data.append(sensor)
            output_data.append(out_val)
            state_data.append(state)
            if len(t_data) > MAX_POINTS:
                del t_data[0]; del sensor_data[0]
                del output_data[0]; del state_data[0]

        if not t_data:
            return line_sen, line_out

        now  = t_data[-1]
        t_lo = max(0, now - WINDOW_S)

        # Slice to window
        xs = [t for t in t_data if t >= t_lo]
        n  = len(xs)
        ys_sen = sensor_data[-n:]
        ys_out = output_data[-n:]

        line_sen.set_data(xs, ys_sen)
        line_out.set_data(xs, ys_out)

        ax_sen.set_xlim(t_lo, t_lo + WINDOW_S)
        ax_out.set_xlim(t_lo, t_lo + WINDOW_S)

        # Show results if available
        if results:
            result_text.set_text(
                f"✓ AUTO-TUNE DONE\n"
                f"Kp = {results['Kp']:.4f}\n"
                f"Kd = {results['Kd']:.4f}\n"
                f"Copy into maglev_simple.ino"
            )

        return line_sen, line_out

    return update


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    global _serial_h

    port = find_port(sys.argv)

    # Open serial ONCE — shared by reader thread and command sender
    try:
        _serial_h = serial.Serial(port, BAUD, timeout=1)
        print(f'[autotuner] Port {port} opened at {BAUD} baud.')
    except serial.SerialException as e:
        print(f'ERROR opening {port}: {e}')
        sys.exit(1)

    # Reader thread (uses the already-open handle)
    reader = threading.Thread(target=serial_reader, args=(_serial_h,), daemon=True)
    reader.start()

    # Build figure
    fig, (ax_sen, ax_out), (line_sen, line_out), result_text = build_figure()
    updater = make_updater(ax_sen, ax_out, line_sen, line_out, result_text)


    # ── Keyboard handler — press keys IN THE PLOT WINDOW ─────────────────────
    def on_key(event):
        key = (event.key or '').upper()
        if key == 'T':
            send_cmd('T')
            print('[autotuner] Sent: T  (start tune)')
        elif key == 'X':
            send_cmd('X')
            print('[autotuner] Sent: X  (abort)')
        elif key == 'R':
            send_cmd('R')
            print('[autotuner] Sent: R  (arm PID)')

    fig.canvas.mpl_connect('key_press_event', on_key)

    # On-screen shortcut hint
    ax_sen.text(0.5, 0.04,
                'Keys (click plot first):  T = Start Tune    X = Abort    R = Arm PID',
                transform=ax_sen.transAxes, ha='center', va='bottom',
                color='#ffa657', fontsize=9,
                bbox=dict(facecolor='#1a1a2e', edgecolor='#ffa657',
                          boxstyle='round,pad=0.3', alpha=0.8))

    ani = animation.FuncAnimation(fig, updater, interval=80, blit=False,
                                  cache_frame_data=False)

    print('\n[autotuner] Plot open.')
    print('  >> Click the plot window, then press  T  to start tuning.')
    print('  >> Press  X  to abort,  R  to arm PID after tuning.\n')
    plt.tight_layout()
    plt.show()


if __name__ == '__main__':
    main()
