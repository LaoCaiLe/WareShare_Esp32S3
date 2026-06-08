"""
PC 硬件数据采集诊断脚本
测试每种数据采集方法是否可用，验证准确性
用法: python diagnose.py
"""
import sys
import os
import json
import time
import subprocess
import platform

# ============================================================
# 1. psutil 基础数据（100% 可用，无需管理员）
# ============================================================
def test_psutil():
    print("=" * 60)
    print("[psutil] Basic Data")
    print("=" * 60)

    import psutil

    # --- CPU ---
    cpu_percent = psutil.cpu_percent(interval=1)
    cpu_freq = psutil.cpu_freq()
    cpu_count_physical = psutil.cpu_count(logical=False)
    cpu_count_logical = psutil.cpu_count(logical=True)

    print(f"\n  CPU:")
    print(f"   使用率:        {cpu_percent}%")
    if cpu_freq:
        print(f"   当前频率:      {cpu_freq.current:.0f} MHz")
        print(f"   最大频率:      {cpu_freq.max:.0f} MHz")
    print(f"   物理核心:      {cpu_count_physical}")
    print(f"   逻辑核心:      {cpu_count_logical}")

    # CPU 名称 (不依赖 psutil)
    try:
        import cpuinfo
        cpu_name = cpuinfo.get_cpu_info()['brand_raw']
    except ImportError:
        # 用注册表读
        try:
            import winreg
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
            cpu_name = winreg.QueryValueEx(key, "ProcessorNameString")[0]
        except:
            cpu_name = platform.processor()
    print(f"   型号:          {cpu_name}")

    # --- 内存 ---
    mem = psutil.virtual_memory()
    swap = psutil.swap_memory()
    print(f"\n 内存:")
    print(f"   已用:          {mem.used / (1024**3):.1f} GB")
    print(f"   总量:          {mem.total / (1024**3):.1f} GB")
    print(f"   使用率:        {mem.percent}%")
    if swap.total > 0:
        print(f"   虚拟内存已用:  {swap.used / (1024**3):.1f} GB")
        print(f"   虚拟内存总量:  {swap.total / (1024**3):.1f} GB")

    # --- 磁盘 ---
    print(f"\n 磁盘:")
    for part in psutil.disk_partitions():
        try:
            usage = psutil.disk_usage(part.mountpoint)
            print(f"   {part.device} ({part.mountpoint})")
            print(f"      已用: {usage.used / (1024**3):.1f} GB / "
                  f"总量: {usage.total / (1024**3):.1f} GB "
                  f"({usage.percent}%)")
        except:
            print(f"   {part.device}: 无法访问")

    # --- 开机时间 ---
    boot_time = psutil.boot_time()
    uptime_seconds = time.time() - boot_time
    days = int(uptime_seconds // 86400)
    hours = int((uptime_seconds % 86400) // 3600)
    minutes = int((uptime_seconds % 3600) // 60)
    print(f"\n  开机时间: {days}天 {hours}小时 {minutes}分")
    print(f"   启动于: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(boot_time))}")

    return {
        "cpu_usage": cpu_percent,
        "cpu_freq": cpu_freq.current if cpu_freq else 0,
        "cpu_freq_max": cpu_freq.max if cpu_freq else 0,
        "cpu_cores_physical": cpu_count_physical,
        "cpu_cores_logical": cpu_count_logical,
        "cpu_name": cpu_name,
        "ram_used_gb": mem.used / (1024**3),
        "ram_total_gb": mem.total / (1024**3),
        "ram_percent": mem.percent,
        "uptime_seconds": uptime_seconds,
    }


# ============================================================
# 2. CPU 温度（WMI 方法，不一定可用）
# ============================================================
def test_cpu_temp_wmi():
    print(f"\n{'=' * 60}")
    print("  CPU 温度 (WMI)")
    print("=" * 60)

    methods = []

    # 方法 A: MSAcpi_ThermalZoneTemperature
    try:
        import wmi
        w = wmi.WMI()
        sensors = w.MSAcpi_ThermalZoneTemperature()
        if sensors:
            for s in sensors:
                # 开尔文温度 × 10 → 摄氏度
                temp = (s.CurrentTemperature / 10.0) - 273.15
                print(f"    MSAcpi_ThermalZone: {temp:.1f}°C (原始: {s.CurrentTemperature})")
                methods.append(("MSAcpi_ThermalZone", temp))
        else:
            print(f"    MSAcpi_ThermalZone: 无传感器数据（主板不支持）")
    except ImportError:
        print(f"     WMI 未安装，运行: pip install wmi")
    except Exception as e:
        print(f"    MSAcpi_ThermalZone: {e}")

    # 方法 B: Win32_PerfFormattedData
    try:
        import wmi
        w = wmi.WMI()
        temps = w.Win32_PerfFormattedData_Counters_ThermalZoneInformation()
        if temps:
            for t in temps:
                temp = (t.HighPrecisionTemperature - 2731.5) / 10.0 if t.HighPrecisionTemperature else None
                if temp and temp > 0 and temp < 125:
                    print(f"    Win32_ThermalZone: {temp:.1f}°C")
                    methods.append(("Win32_ThermalZone", temp))
                else:
                    print(f"     Win32_ThermalZone: 温度异常 ({temp}°C)")
        else:
            print(f"    Win32_ThermalZone: 无数据")
    except Exception as e:
        print(f"    Win32_ThermalZone: {e}")

    # 方法 C: Win32_Processor LoadPercentage (没有温度，但有负载)
    try:
        import wmi
        w = wmi.WMI()
        for cpu in w.Win32_Processor():
            print(f"     Win32_Processor: {cpu.Name[:60]}...")
            if hasattr(cpu, 'LoadPercentage'):
                print(f"      负载: {cpu.LoadPercentage}%")
            break
    except:
        pass

    if not methods:
        print(f"\n     所有 WMI 方法均无法获取 CPU 温度")
        print(f"   原因: Windows 台式机通常不暴露 ACPI 热区")
        print(f"   备选: LibreHardwareMonitor 或 CoreTemp 共享内存")

    return methods


# ============================================================
# 3. GPU 数据（nvidia-smi）
# ============================================================
def test_gpu_nvidia():
    print(f"\n{'=' * 60}")
    print(" GPU 数据 (nvidia-smi)")
    print("=" * 60)

    try:
        result = subprocess.run(
            [
                "nvidia-smi",
                "--query-gpu=index,name,temperature.gpu,utilization.gpu,memory.used,memory.total,power.draw",
                "--format=csv,noheader,nounits"
            ],
            capture_output=True, text=True, timeout=10
        )

        if result.returncode != 0:
            print(f"    nvidia-smi 返回错误: {result.stderr.strip()}")
            return []

        gpus = []
        for line in result.stdout.strip().split('\n'):
            if not line.strip():
                continue
            parts = [p.strip() for p in line.split(',')]
            idx, name, temp, util, mem_used, mem_total, power = parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], parts[6]

            print(f"\n    GPU {idx}: {name}")
            print(f"      温度: {temp}°C")
            print(f"      使用率: {util}%")
            print(f"      显存: {mem_used} / {mem_total} MB")
            print(f"      功耗: {power} W")

            gpus.append({
                "index": idx,
                "name": name,
                "temp": int(temp) if temp.isdigit() else -1,
                "usage": int(util) if util.isdigit() else -1,
                "mem_used_mb": int(mem_used) if mem_used.isdigit() else -1,
                "mem_total_mb": int(mem_total) if mem_total.isdigit() else -1,
                "power_w": float(power) if power.replace('.','').isdigit() else -1,
            })

        return gpus

    except FileNotFoundError:
        print(f"    nvidia-smi 未找到（无 NVIDIA GPU 或驱动未安装）")
        return []
    except Exception as e:
        print(f"    nvidia-smi 错误: {e}")
        return []


# ============================================================
# 4. GPU 数据（WMI，通用但信息少）
# ============================================================
def test_gpu_wmi():
    print(f"\n{'=' * 60}")
    print(" GPU 数据 (WMI)")
    print("=" * 60)

    try:
        import wmi
        w = wmi.WMI()
        gpus = w.Win32_VideoController()
        if gpus:
            for gpu in gpus:
                print(f"\n    {gpu.Name}")
                print(f"      驱动版本: {gpu.DriverVersion}")
                print(f"      显存: {gpu.AdapterRAM}")
                if gpu.AdapterRAM:
                    ram_gb = int(gpu.AdapterRAM) / (1024**3)
                    print(f"      显存: {ram_gb:.1f} GB")
                if hasattr(gpu, 'CurrentHorizontalResolution'):
                    print(f"      分辨率: {gpu.CurrentHorizontalResolution}x{gpu.CurrentVerticalResolution}")
        else:
            print(f"    无 GPU 信息")
    except Exception as e:
        print(f"    WMI GPU: {e}")


# ============================================================
# 5. 磁盘温度（SMART）
# ============================================================
def test_disk_temp():
    print(f"\n{'=' * 60}")
    print(" 磁盘温度 (WMI SMART)")
    print("=" * 60)

    try:
        import wmi
        w = wmi.WMI()

        # MSStorageDriver 读取 SMART 温度
        for disk in w.Win32_DiskDrive():
            print(f"\n    {disk.Model.strip()} ({disk.Size})")
            if disk.Size:
                size_gb = int(disk.Size) / (1024**3)
                print(f"      容量: {size_gb:.0f} GB")

            # 尝试读取 S.M.A.R.T 温度
            try:
                # 通过关联类获取磁盘到存储的关系
                for param in disk.associators(wmi_result_class="MSStorageDriver_ATAPISmartData"):
                    # SMART 数据比较复杂，这里简化
                    pass
            except:
                pass

        # 备选: MSFT_PhysicalDisk (Windows 10+)
        try:
            phys_disks = w.query("SELECT * FROM MSFT_PhysicalDisk")
            for pd in phys_disks:
                if hasattr(pd, 'Temperature') and pd.Temperature:
                    print(f"    {pd.FriendlyName}: {pd.Temperature}°C")
                if hasattr(pd, 'HealthStatus') and pd.HealthStatus:
                    print(f"      健康状态: {pd.HealthStatus}")
        except:
            print(f"     MSFT_PhysicalDisk 不支持（需要 Win10+ / 管理员权限）")

    except Exception as e:
        print(f"    磁盘 SMART: {e}")


# ============================================================
# 6. 模拟 JSON 输出（最终 ESP32 收到的数据格式）
# ============================================================
def generate_json(basic, gpus):
    print(f"\n{'=' * 60}")
    print(" 模拟 ESP32 数据格式 (JSON)")
    print("=" * 60)

    # 组装 CPU 数据
    cpu = {
        "name": basic["cpu_name"],
        "usage": round(basic["cpu_usage"], 1),
        "temp": -1,  # 温度需另行获取
        "freq": round(basic["cpu_freq"]),
        "cores": basic["cpu_cores_logical"],
    }

    # 组装 GPU 数据
    gpu_list = []
    for g in gpus:
        gpu_list.append({
            "name": g["name"],
            "usage": g["usage"],
            "temp": g["temp"],
            "mem_used": g["mem_used_mb"],
            "mem_total": g["mem_total_mb"],
        })

    # 组装内存数据
    ram = {
        "used": round(basic["ram_used_gb"] * 1024),  # MB
        "total": round(basic["ram_total_gb"] * 1024),  # MB
    }

    # 组装磁盘数据
    import psutil
    disks = []
    for part in psutil.disk_partitions():
        try:
            usage = psutil.disk_usage(part.mountpoint)
            disks.append({
                "name": part.device.rstrip("\\"),
                "used": round(usage.used / (1024**2)),   # MB
                "total": round(usage.total / (1024**2)),  # MB
                "temp": -1,  # SMART温度需单独获取
            })
        except:
            pass

    # 完整 JSON
    data = {
        "ts": int(time.time()),
        "cpu": cpu,
        "gpu": gpu_list[0] if gpu_list else None,
        "gpus": gpu_list,
        "ram": ram,
        "disk": disks,
        "uptime": int(basic["uptime_seconds"]),
    }

    json_str = json.dumps(data, ensure_ascii=False, indent=2)
    print(json_str)
    print(f"\n   JSON 大小: {len(json_str)} 字节")

    return data


# ============================================================
# 主流程
# ============================================================
def main():
    print("\n" + "=" * 60)
    print("     PC Hardware Diagnostic Tool")
    print("=" * 60)

    print(f"\n系统: {platform.system()} {platform.release()} {platform.machine()}")
    print(f"Python: {sys.version}")

    # Step 1: psutil 基础数据
    basic = test_psutil()

    # Step 2: CPU 温度
    cpu_temps = test_cpu_temp_wmi()

    # Step 3: NVIDIA GPU
    gpus = test_gpu_nvidia()

    # Step 4: WMI GPU (备用)
    if not gpus:
        test_gpu_wmi()

    # Step 5: 磁盘温度
    test_disk_temp()

    # Step 6: 生成目标 JSON
    generate_json(basic, gpus)

    # 总结
    print(f"\n{'=' * 60}")
    print(" 采集能力总结")
    print("=" * 60)

    checks = [
        ("CPU 使用率/频率/核心数", True),
        ("CPU 温度", len(cpu_temps) > 0),
        ("RAM 使用率", True),
        ("磁盘空间", True),
        ("GPU 温度/使用率", len(gpus) > 0),
        ("开机时间", True),
        ("磁盘温度", False),  # 默认不可用
    ]

    for name, ok in checks:
        print(f"   {'' if ok else ''} {name}")

    print(f"\n数据采集率: {sum(1 for _, ok in checks if ok)}/{len(checks)}")

    if len(cpu_temps) == 0 and len(gpus) == 0:
        print(f"\n  温度传感器均不可用，建议:")
        print(f"   1. 安装 LibreHardwareMonitor 并用其 API")
        print(f"   2. 或安装 CoreTemp 并读其共享内存")
        print(f"   3. 或直接用 psutil + nvidia-smi 组合（够用）")

    print(f"\n 诊断完成！\n")


if __name__ == "__main__":
    main()
