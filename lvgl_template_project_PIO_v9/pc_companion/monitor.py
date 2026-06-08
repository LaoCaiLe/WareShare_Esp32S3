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
                "--query-gpu=name,temperature.gpu,utilization.gpu,memory.used,memory.total,power.draw",
                "--format=csv,noheader,nounits"
            ],
            capture_output=True, text=True, timeout=8
        )
        if result.returncode != 0:
            return None

        line = result.stdout.strip().split('\n')[0]
        parts = [p.strip() for p in line.split(',')]
        name, temp, util, mem_used, mem_total, power = parts

        return {
            "name": name,
            "usage": int(util) if util.isdigit() else 0,
            "temp": int(temp) if temp.isdigit() else -1,
            "mem_used": int(mem_used) if mem_used.isdigit() else 0,
            "mem_total": int(mem_total) if mem_total.isdigit() else 0,
            "power_w": int(float(power)) if power.replace('.', '').isdigit() else 0,
        }
    except FileNotFoundError:
        return None  # No NVIDIA GPU
    except Exception as e:
        print(f"[GPU] nvidia-smi error: {e}")
        return None


def get_ram_data():
    """Collect RAM usage."""
    import psutil
    mem = psutil.virtual_memory()
    return {
        "used": mem.used // (1024 * 1024),   # MB
        "total": mem.total // (1024 * 1024),  # MB
    }


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


def collect_all():
    """Collect all hardware data and return as dict."""
    return {
        "ts": int(time.time()),
        "cpu": get_cpu_data(),
        "gpu": get_gpu_data(),
        "ram": get_ram_data(),
        "disk": get_disk_data(),
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
    try:
        while True:
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
