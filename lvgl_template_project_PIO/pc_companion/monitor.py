"""
PC Hardware Monitor — Windows Companion App

Collects CPU/GPU/RAM/Disk data and broadcasts one JSON datagram per second
over WiFi to the ESP32-S3 for real-time LVGL dashboard display.

Usage:
    python monitor.py                # broadcast JSON over UDP
    python monitor.py --dry-run      # print JSON without sending

Dependencies: pip install psutil
Optional: run LibreHardwareMonitor (as admin) with
"Options -> Remote Web Server -> Run" for CPU temperature.
"""
import os
import time
import json
import re
import socket
import struct
import subprocess
import argparse
import threading
import urllib.request
from datetime import datetime

# ============================================================
# Constants
# ============================================================
UDP_PORT = 4210  # keep in sync with NET_UDP_PORT in src/net_config.h
# Run windowless (pythonw) and every console helper we spawn would pop a new
# console window — nvidia-smi runs once per frame, so the taskbar flickered.
CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
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


# CPU temperature comes from LibreHardwareMonitor: Windows itself exposes no
# CPU sensor through psutil, and the ACPI thermal zone on desktop boards
# (verified here under full load) stays flat. LHM reads the CPU's own thermal
# sensors and publishes them through its web server (Options -> Remote Web
# Server -> Run, default port 8085).
# 127.0.0.1 rather than "localhost": on Windows the IPv6 localhost attempt
# burns the whole 2 s timeout before falling back to IPv4.
# Only a failed localhost connect is attempted when LHM is not running, so
# this probe costs nothing in the normal (LHM closed) case.
_LHM_URL = "http://127.0.0.1:8085/data.json"

_lhm_temp = None
_lhm_last_try = 0.0
_lhm_probing = False
_LHM_OK_TTL = 10.0     # refresh a good reading every 10 s
_LHM_FAIL_TTL = 60.0   # retry slowly while LHM is not running


_LHM_NUM = re.compile(r"[-+]?\d+(?:[.,]\d+)?")


def _lhm_number(value):
    """Coerce an LHM value: a number, or a formatted string like "52.0 °C"."""
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        match = _LHM_NUM.search(value)
        if match:
            try:
                return float(match.group(0).replace(",", "."))
            except ValueError:
                return None
    return None


def _lhm_temp_from_web():
    """CPU package temperature from LHM's web server, or None."""
    try:
        with urllib.request.urlopen(_LHM_URL, timeout=2) as resp:
            data = json.load(resp)
    except Exception:
        return None

    package = tctl = core_max = None
    stack = [data]
    while stack:
        node = stack.pop()
        stack.extend(node.get("Children") or ())
        if node.get("Type") != "Temperature":
            continue
        sensor_id = node.get("SensorId", "")
        if not (sensor_id.startswith("/intelcpu/")
                or sensor_id.startswith("/amdcpu/")):
            continue
        name = node.get("Text", "")
        # "Distance to TjMax" is thermal headroom, not a temperature.
        if "Distance" in name:
            continue
        # Older LHM builds emit RawValue as a number; newer ones emit a
        # formatted string, so parse both.
        raw = _lhm_number(node.get("RawValue"))
        if raw is None:
            raw = _lhm_number(node.get("Value"))
        if raw is None or raw <= 0:
            continue
        if "Package" in name:
            package = max(package or 0.0, raw)
        elif "Tctl" in name or "Tdie" in name:
            tctl = max(tctl or 0.0, raw)
        else:
            core_max = max(core_max or 0.0, raw)

    if package is not None:
        return package
    if tctl is not None:
        return tctl
    return core_max


def _refresh_lhm_temp():
    """Probe LHM once and update the cached temperature."""
    global _lhm_temp, _lhm_last_try, _lhm_probing
    try:
        value = _lhm_temp_from_web()
        if value is not None:
            value = int(round(value))
            if not (0 < value < 127):   # ESP32 keeps this in an int8_t
                value = None
        _lhm_temp = value
    finally:
        _lhm_last_try = time.time()
        _lhm_probing = False


def get_cpu_temp_c():
    """CPU package temperature in Celsius, or None if LHM is not available.

    Requires LibreHardwareMonitor running in the background (as admin);
    its kernel driver is what actually reads the CPU's thermal sensors.
    Probing happens on a worker thread so a missing LHM never stalls the
    one-second send loop.
    """
    global _lhm_probing

    now = time.time()
    ttl = _LHM_OK_TTL if _lhm_temp is not None else _LHM_FAIL_TTL
    if not _lhm_probing and now - _lhm_last_try >= ttl:
        _lhm_probing = True
        threading.Thread(target=_refresh_lhm_temp, daemon=True).start()
    return _lhm_temp


class _PdhCpuCounters:
    """Read Task Manager's CPU counters via the Windows PDH API.

    % Processor Utility is the CPU percentage Task Manager shows (it folds in
    frequency scaling, so it reads well above psutil's idle-based % Processor
    Time). % Processor Performance is the current clock as a percentage of the
    base clock; Task Manager's "Speed" is base MHz x this / 100. psutil's
    cpu_freq() only returns Win32_Processor.CurrentClockSpeed, which on this
    machine stays pinned at the 3.0 GHz base even while the cores boost.

    PDH averages the counters between reads, and the companion reads once per
    frame, so each value covers the whole frame interval.
    """

    _FMT_DOUBLE = 0x00000200

    _UTILITY = r"\Processor Information(_Total)\% Processor Utility"
    _PERFORMANCE = r"\Processor Information(_Total)\% Processor Performance"

    def __init__(self):
        import ctypes
        from ctypes import wintypes

        self._ct = ctypes
        self._wt = wintypes

        class _FmtValue(ctypes.Structure):
            _fields_ = [("status", wintypes.DWORD),
                        ("value", ctypes.c_double)]

        self._fmt = _FmtValue

        pdh = ctypes.WinDLL("pdh.dll")
        self._pdh = pdh
        pdh.PdhOpenQueryW.argtypes = [
            wintypes.LPCWSTR, ctypes.c_size_t,
            ctypes.POINTER(wintypes.HANDLE)]
        pdh.PdhOpenQueryW.restype = ctypes.c_long
        pdh.PdhAddEnglishCounterW.argtypes = [
            wintypes.HANDLE, wintypes.LPCWSTR, ctypes.c_size_t,
            ctypes.POINTER(wintypes.HANDLE)]
        pdh.PdhAddEnglishCounterW.restype = ctypes.c_long
        pdh.PdhCollectQueryData.argtypes = [wintypes.HANDLE]
        pdh.PdhCollectQueryData.restype = ctypes.c_long
        pdh.PdhGetFormattedCounterValue.argtypes = [
            wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
            ctypes.POINTER(_FmtValue)]
        pdh.PdhGetFormattedCounterValue.restype = ctypes.c_long

        self._query = wintypes.HANDLE()
        if pdh.PdhOpenQueryW(None, 0, ctypes.byref(self._query)) != 0:
            raise OSError("PdhOpenQueryW failed")

        self._utility = self._add(self._UTILITY)
        if self._utility is None:
            pdh.PdhCloseQuery(self._query)
            raise OSError("PdhAddEnglishCounterW failed")

        # Optional: some Windows builds do not expose it.
        self._performance = self._add(self._PERFORMANCE)

        pdh.PdhCollectQueryData(self._query)   # baseline

    def _add(self, path):
        handle = self._wt.HANDLE()
        if self._pdh.PdhAddEnglishCounterW(
                self._query, path, 0, self._ct.byref(handle)) != 0:
            return None
        return handle

    def _value(self, counter):
        if counter is None:
            return None
        val = self._fmt()
        status = self._pdh.PdhGetFormattedCounterValue(
            counter, self._FMT_DOUBLE, None, self._ct.byref(val))
        if status != 0 or val.status != 0:
            return None
        return val.value

    def read(self):
        """(cpu_utility, processor_performance); either may be None."""
        if self._pdh.PdhCollectQueryData(self._query) != 0:
            return None, None
        return self._value(self._utility), self._value(self._performance)


_cpu_counters = None
_cpu_counters_unavailable = False


def _read_cpu_counters():
    """(utility %, performance %) or (None, None) when PDH is unavailable."""
    global _cpu_counters, _cpu_counters_unavailable

    if _cpu_counters_unavailable:
        return None, None
    if _cpu_counters is None:
        try:
            _cpu_counters = _PdhCpuCounters()
        except Exception:
            _cpu_counters_unavailable = True
            return None, None
    try:
        return _cpu_counters.read()
    except Exception:
        _cpu_counters_unavailable = True
        return None, None


_cpu_base_mhz = None
_cpu_base_mhz_read = False


def _get_cpu_base_mhz():
    """Nominal clock (Task Manager's base speed) from the registry."""
    global _cpu_base_mhz, _cpu_base_mhz_read

    if not _cpu_base_mhz_read:
        _cpu_base_mhz_read = True
        try:
            import winreg
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
            value = winreg.QueryValueEx(key, "~MHz")[0]
            _cpu_base_mhz = float(value) if value else None
        except Exception:
            _cpu_base_mhz = None
    return _cpu_base_mhz


def get_cpu_data():
    """Collect CPU usage, frequency, cores, temperature, process count."""
    import psutil

    # Prefer Task Manager's metrics; fall back to psutil where unavailable.
    utility, performance = _read_cpu_counters()
    if utility is not None:
        usage = max(0.0, min(100.0, utility))
    else:
        usage = psutil.cpu_percent(interval=0.5)

    # Task Manager's "Speed" = base clock x % Processor Performance / 100.
    # cpu_freq() on its own reports the static base clock on this machine.
    base_mhz = _get_cpu_base_mhz()
    if performance is not None and performance > 0 and base_mhz:
        freq = round(base_mhz * performance / 100.0)
    else:
        freq_info = psutil.cpu_freq()
        freq = round(freq_info.current) if freq_info else 0
    cores = psutil.cpu_count(logical=True)
    temp = get_cpu_temp_c()
    # No CPU temperature without a driver-backed helper, so the device
    # shows the process count in that slot instead.
    procs = len(psutil.pids())

    return {
        "name": get_cpu_name(),
        "usage": round(usage, 1),
        "temp": temp if temp is not None else -1,
        "freq": freq,
        "cores": cores,
        "procs": procs,
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
            capture_output=True, text=True, timeout=8,
            creationflags=CREATE_NO_WINDOW
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
        # Only fixed disks (skip removable, CD-ROM, network drives)
        if 'fixed' not in part.opts.lower():
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


_disk_phys_total_mb = None


def get_disk_phys_total_mb():
    """Total capacity of the physical disks, in MiB.

    Volume totals skip recovery/ESP partitions and unmounted space, so they
    read lower than the drives' rated capacity. Queried once — the disks do
    not change while the companion is running.
    """
    global _disk_phys_total_mb
    if _disk_phys_total_mb is None:
        try:
            out = subprocess.check_output(
                ["powershell", "-NoProfile", "-NonInteractive", "-Command",
                 "[uint64]((Get-CimInstance Win32_DiskDrive | "
                 "Measure-Object Size -Sum).Sum)"],
                text=True, timeout=10,
                creationflags=CREATE_NO_WINDOW).strip()
            _disk_phys_total_mb = int(out) // (1024 * 1024)
        except Exception:
            _disk_phys_total_mb = 0
    return _disk_phys_total_mb


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
        "disk_phys_total": get_disk_phys_total_mb(),
        "net": get_net_speed(),
        "loadavg": get_loadavg(),
        "uptime": get_uptime(),
    }


# ============================================================
# Transport (UDP broadcast)
# ============================================================

def get_broadcast_targets():
    """Directed broadcast address of every active IPv4 interface.

    Sending to each LAN's own broadcast (e.g. 192.168.10.255) reaches the
    dial regardless of which interface Windows picks for 255.255.255.255.
    APIPA (169.254.x.x) and loopback interfaces are skipped.
    """
    import psutil

    targets = set()
    for addrs in psutil.net_if_addrs().values():
        for addr in addrs:
            if addr.family != socket.AF_INET or not addr.netmask:
                continue
            if addr.address.startswith(("127.", "169.254.")):
                continue
            try:
                ip = struct.unpack("!I", socket.inet_aton(addr.address))[0]
                mask = struct.unpack("!I", socket.inet_aton(addr.netmask))[0]
            except OSError:
                continue
            bcast = ip | (~mask & 0xFFFFFFFF)
            targets.add(socket.inet_ntoa(struct.pack("!I", bcast)))

    if not targets:
        targets.add("255.255.255.255")
    return sorted(targets)


def run_udp(port=UDP_PORT):
    """Main loop: collect data and broadcast it over UDP."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    targets = get_broadcast_targets()
    print(f"Broadcasting UDP on port {port} to: {', '.join(targets)}")
    print("Press Ctrl+C to stop.\n")

    frame_count = 0
    try:
        while True:
            data = collect_all()
            payload = json.dumps(data, ensure_ascii=False).encode("utf-8")
            for target in targets:
                try:
                    sock.sendto(payload, (target, port))
                except OSError:
                    pass  # interface went away; refreshed below
            frame_count += 1

            # Interfaces come and go (WiFi on/off, VPN); refresh occasionally.
            if frame_count % 10 == 0:
                targets = get_broadcast_targets()

            cpu_str = f"CPU {data['cpu']['usage']}%"
            gpu_str = (f"GPU {data['gpu']['temp']}°C {data['gpu']['usage']}%"
                       if data['gpu'] else "GPU N/A")
            ram_str = (f"RAM {data['ram']['used']/1024:.1f}/"
                       f"{data['ram']['total']/1024:.1f}G")
            print(f"[{frame_count:04d}] {cpu_str} | {gpu_str} | {ram_str} | "
                  f"{len(payload)}B", end='\r')

            time.sleep(INTERVAL)
    except KeyboardInterrupt:
        print(f"\n\nStopped. Sent {frame_count} frames.")
    finally:
        sock.close()

def run_dry_run():
    """Print JSON to console without sending (for testing)."""
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
  python monitor.py                 Broadcast over WiFi (UDP)
  python monitor.py --dry-run       Print JSON without sending
        """
    )
    parser.add_argument("--udp-port", type=int, default=UDP_PORT,
                        help=f"UDP broadcast port (default: {UDP_PORT})")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print JSON to console, don't send")
    parser.add_argument("--interval", type=float, default=INTERVAL,
                        help=f"Update interval in seconds (default: {INTERVAL})")

    args = parser.parse_args()
    INTERVAL = args.interval

    if args.dry_run:
        run_dry_run()
        return

    run_udp(args.udp_port)


if __name__ == "__main__":
    main()
