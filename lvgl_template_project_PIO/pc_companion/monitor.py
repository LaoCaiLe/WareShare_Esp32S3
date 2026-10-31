"""
PC Hardware Monitor — Windows Companion App

Collects CPU/GPU/RAM/Disk data and sends JSON frames over USB Serial
to ESP32-S3 for real-time LVGL dashboard display.

Usage:
    python monitor.py COM3           # auto-detect COM port
    python monitor.py COM3 --dry-run  # print JSON without serial
    python monitor.py --list          # list available COM ports

Frame format: [STX:0x02][JSON][ETX:0x03]

Dependencies: pip install psutil pyserial
"""
import sys
import os
import time
import json
import subprocess
import argparse
from datetime import datetime

# ============================================================
# Constants
# ============================================================
STX = b'\x02'
ETX = b'\x03'
BAUD_RATE = 921600
INTERVAL = 1.0  # seconds between updates


# ============================================================
# Data Collectors
# ============================================================

def get_cpu_name():
    """Read CPU name from Windows registry."""
    try:
        import winreg
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
            r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
        return winreg.QueryValueEx(key, "ProcessorNameString")[0]
    except:
        import platform
        return platform.processor()


def get_cpu_data():
    """Collect CPU usage, frequency, cores."""
    import psutil

    usage = psutil.cpu_percent(interval=0.5)
    freq_info = psutil.cpu_freq()
    freq = round(freq_info.current) if freq_info else 0
    cores = psutil.cpu_count(logical=True)

    return {
        "name": get_cpu_name(),
        "usage": round(usage, 1),
        "temp": -1,     # Desktop CPUs typically don't expose temp via WMI
        "freq": freq,
        "cores": cores,
    }


def get_gpu_data():
    """Collect GPU data via nvidia-smi (NVIDIA only)."""
    try:
        result = subprocess.run(
            [
                "nvidia-smi",
                "--query-gpu=name,temperature.gpu,utilization.gpu,memory.used,memory.total,power.draw,clocks.current.graphics,clocks.current.memory,fan.speed",
                "--format=csv,noheader,nounits"
            ],
            capture_output=True, text=True, timeout=8
        )
        if result.returncode != 0:
            return None

        line = result.stdout.strip().split('\n')[0]
        parts = [p.strip() for p in line.split(',')]
        # name, temp, util, mem_used, mem_total, power, core_clock, mem_clock, fan_speed
        name      = parts[0] if len(parts) > 0 else "Unknown"
        temp      = parts[1] if len(parts) > 1 else "-1"
        util      = parts[2] if len(parts) > 2 else "0"
        mem_used  = parts[3] if len(parts) > 3 else "0"
        mem_total = parts[4] if len(parts) > 4 else "0"
        power     = parts[5] if len(parts) > 5 else "0"
        core_clk  = parts[6] if len(parts) > 6 else "0"
        mem_clk   = parts[7] if len(parts) > 7 else "0"
        fan       = parts[8] if len(parts) > 8 else "0"

        def safe_int(s): return int(s) if s.strip().isdigit() else 0

        return {
            "name": name,
            "usage": safe_int(util),
            "temp": safe_int(temp) if temp.strip().lstrip('-').isdigit() else -1,
            "mem_used": safe_int(mem_used),
            "mem_total": safe_int(mem_total),
            "power_w": safe_int(power.split('.')[0]) if power.replace('.','').strip().isdigit() else 0,
            "core_clock": safe_int(core_clk),
            "mem_clock": safe_int(mem_clk),
            "fan_speed": safe_int(fan),
        }
    except FileNotFoundError:
        return None  # No NVIDIA GPU
    except Exception as e:
        print(f"[GPU] nvidia-smi error: {e}")
        return None


def get_ram_data():
    """Collect RAM and swap usage."""
    import psutil
    mem = psutil.virtual_memory()
    swap = psutil.swap_memory()
    mb = 1024 * 1024
    return {
        "used": mem.used // mb,       # MB
        "total": mem.total // mb,     # MB
        "swap_used": swap.used // mb,   # MB
        "swap_total": swap.total // mb, # MB
    }


def get_loadavg():
    """Collect 1/5/15-minute load average.

    psutil emulates load average on Windows; the first call can block for
    about 5 seconds while it warms up.
    """
    try:
        import psutil
        l1, l5, l15 = psutil.getloadavg()
        return [round(l1, 2), round(l5, 2), round(l15, 2)]
    except Exception:
        return [0.0, 0.0, 0.0]


def get_disk_data():
    """Collect disk usage for all fixed drives."""
    import psutil
    disks = []
    for part in psutil.disk_partitions():
        # Only fixed disks (skip removable, CD-ROM)
        if 'fixed' not in part.opts.lower() and 'cdrom' in part.opts.lower():
            continue
        try:
            usage = psutil.disk_usage(part.mountpoint)
            disks.append({
                "name": part.device.rstrip("\\"),
                "used": usage.used // (1024 * 1024),    # MB
                "total": usage.total // (1024 * 1024),  # MB
                "temp": -1,  # SMART temp not available via psutil
            })
        except:
            pass
    return disks


def get_uptime():
    """Get system uptime in seconds."""
    import psutil
    return int(time.time() - psutil.boot_time())


# Network speed calculation (stateful: stores previous counters)
_net_prev_bytes = None
_net_prev_time = None

def get_net_speed():
    """Get network up/down speed in KB/s using delta from previous call."""
    global _net_prev_bytes, _net_prev_time
    import psutil
    counters = psutil.net_io_counters()
    now = time.time()
    cur_bytes = (counters.bytes_sent, counters.bytes_recv)

    if _net_prev_bytes is not None and _net_prev_time is not None:
        elapsed = now - _net_prev_time
        if elapsed > 0:
            up_delta   = (cur_bytes[0] - _net_prev_bytes[0]) / elapsed
            down_delta = (cur_bytes[1] - _net_prev_bytes[1]) / elapsed
            up_kb   = max(0, int(up_delta / 1024))
            down_kb = max(0, int(down_delta / 1024))
        else:
            up_kb = down_kb = 0
    else:
        up_kb = down_kb = 0

    _net_prev_bytes = cur_bytes
    _net_prev_time = now
    return {
        "up": up_kb,
        "down": down_kb,
        # cumulative counters since boot, KB
        "total_up": counters.bytes_sent // 1024,
        "total_down": counters.bytes_recv // 1024,
    }


def collect_all():
    """Collect all hardware data and return as dict."""
    return {
        "ts": int(time.time()),
        "cpu": get_cpu_data(),
        "gpu": get_gpu_data(),
        "ram": get_ram_data(),
        "disk": get_disk_data(),
        "net": get_net_speed(),
        "loadavg": get_loadavg(),
        "uptime": get_uptime(),
    }


# ============================================================
# Serial Communication
# ============================================================

def list_ports():
    """List available COM ports."""
    import serial.tools.list_ports as lp
    ports = lp.comports()
    if not ports:
        print("No COM ports found")
        return
    for p in ports:
        print(f"  {p.device} - {p.description}")


def send_frame(ser, data):
    """Send a JSON frame over serial."""
    payload = json.dumps(data, ensure_ascii=False).encode('utf-8')
    ser.write(STX)
    ser.write(payload)
    ser.write(ETX)
    ser.flush()
    return len(payload)


def read_esp_messages(ser, buffer):
    """Print text lines sent back by the ESP32 (status / FPS), keep partials."""
    try:
        waiting = ser.in_waiting
        if waiting:
            buffer += ser.read(waiting)
    except Exception:
        return buffer

    while b"\n" in buffer:
        line, buffer = buffer.split(b"\n", 1)
        text = line.decode("utf-8", "replace").strip()
        if text:
            print(f"\r[ESP] {text}")
    return buffer


def run_serial(port):
    """Main loop: collect data and send over serial."""
    import serial

    print(f"Connecting to {port} @ {BAUD_RATE} baud...")
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=1)
        # Wait for ESP32-S3 to boot
        time.sleep(2)
        print(f"Connected. Sending data every {INTERVAL}s...")
        print("Press Ctrl+C to stop.\n")
    except serial.SerialException as e:
        print(f"ERROR: Cannot open {port}: {e}")
        print("Make sure:")
        print("  1. ESP32-S3 is connected via USB")
        print("  2. The port is correct (use --list to see available ports)")
        print("  3. No other program (Serial Monitor) is using the port")
        sys.exit(1)

    frame_count = 0
    rx_text = b""
    try:
        while True:
            rx_text = read_esp_messages(ser, rx_text)
            data = collect_all()
            size = send_frame(ser, data)
            frame_count += 1

            # Pretty print status line
            cpu_str = f"CPU {data['cpu']['usage']}%"
            gpu_str = f"GPU {data['gpu']['temp']}°C {data['gpu']['usage']}%" if data['gpu'] else "GPU N/A"
            ram_str = f"RAM {data['ram']['used']/1024:.1f}/{data['ram']['total']/1024:.1f}G"
            print(f"[{frame_count:04d}] {cpu_str} | {gpu_str} | {ram_str} | {size}B",
                  end='\r')

            time.sleep(INTERVAL)
    except KeyboardInterrupt:
        print(f"\n\nStopped. Sent {frame_count} frames.")
    finally:
        ser.close()


def run_dry_run():
    """Print JSON to console without serial (for testing)."""
    print("=== DRY RUN MODE ===\n")
    data = collect_all()
    print(json.dumps(data, ensure_ascii=False, indent=2))
    print(f"\nJSON size: {len(json.dumps(data, ensure_ascii=False))} bytes")
    print("\nPress Ctrl+C to run continuously...")
    try:
        count = 1
        while True:
            data = collect_all()
            size = len(json.dumps(data, ensure_ascii=False))
            cpu = data['cpu']['usage']
            gpu_temp = data['gpu']['temp'] if data['gpu'] else 'N/A'
            ram_pct = data['ram']['used'] / data['ram']['total'] * 100
            print(f"[{count:04d}] CPU:{cpu}% GPU:{gpu_temp}°C RAM:{ram_pct:.1f}% {size}B", end='\r')
            count += 1
            time.sleep(INTERVAL)
    except KeyboardInterrupt:
        print(f"\n\nDone. Collected {count} samples.")


# ============================================================
# CLI
# ============================================================

def main():
    global INTERVAL
    parser = argparse.ArgumentParser(
        description="PC Hardware Monitor - ESP32-S3 Companion",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python monitor.py COM3            Send data to ESP32 on COM3
  python monitor.py COM3 --dry-run  Print JSON without serial
  python monitor.py --list          List available COM ports
        """
    )
    parser.add_argument("port", nargs="?", help="COM port (e.g., COM3)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print JSON to console, don't use serial")
    parser.add_argument("--list", action="store_true",
                        help="List available COM ports and exit")
    parser.add_argument("--interval", type=float, default=INTERVAL,
                        help=f"Update interval in seconds (default: {INTERVAL})")

    args = parser.parse_args()
    INTERVAL = args.interval

    if args.list:
        list_ports()
        return

    if args.dry_run:
        run_dry_run()
        return

    if not args.port:
        parser.print_help()
        print("\nError: COM port required (e.g., COM3)")
        sys.exit(1)

    run_serial(args.port)


if __name__ == "__main__":
    main()
