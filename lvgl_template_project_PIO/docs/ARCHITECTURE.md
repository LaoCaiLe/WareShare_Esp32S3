# ESP32-S3 硬件监控表盘 — 架构与逻辑说明

本文档描述当前工作区的完整实现：代码结构、数据显示逻辑与通信链路，对应 `lvgl_template_project_PIO`（master 工作区，含 WiFi/UDP 改造）。

---

## 1. 系统概览

### 1.1 硬件

| 项目 | 规格 |
|---|---|
| 开发板 | Waveshare ESP32-S3-Touch-LCD-1.28 |
| 屏幕 | GC9A01，240×240 圆形 IPS，RGB565 |
| 触摸 | CST816S 电容触摸（I2C） |
| 屏幕引脚 | MOSI 11, SCLK 10, CS 9, DC 8, RST 14，SPI 80 MHz |
| 触摸引脚 | SDA 6, SCL 7, RST 13, IRQ 5 |
| 主控 | ESP32-S3，2.4 GHz WiFi（不支持 5 GHz） |

### 1.2 软件栈

| 层 | 实现 |
|---|---|
| 构建 | PlatformIO（Arduino framework），`platformio.ini` |
| GUI | LVGL 8.3.9，`LV_COLOR_DEPTH 16`，双缓冲各半屏（28,800 px） |
| JSON | ArduinoJson 7（静态 `JsonDocument`） |
| 显示驱动 | TFT_eSPI（GC9A01） |
| 触摸驱动 | 自带 `CST816S` 驱动 + 轮询手势识别 |
| PC 端 | `pc_companion/monitor.py`，Python 3 + psutil + 可选 nvidia-smi / LibreHardwareMonitor |

### 1.3 数据链路

```
┌──────────── PC (Windows) ────────────┐        ┌──────── ESP32-S3 ────────┐
│ monitor.py  (~1.5 s/帧)               │  UDP   │ udp_reader.cpp           │
│  psutil / nvidia-smi / LHM / CIM      │ ─────► │  WiFi STA, 端口 4210      │
│  collect_all() → JSON (约 863 B)      │ 广播    │         │                │
│                                       │        │         ▼                │
│  （备用）串口 STX+JSON+ETX, 921600     │ 串口   │ serial_reader.cpp        │
└───────────────────────────────────────┘ ─────► │         │                │
                                                  │         ▼                │
                                                  │ serial_reader_parse_json │
                                                  │         │ pc_data_t      │
                                                  │         ▼                │
                                                  │ sampler → sys_sample_t   │
                                                  │         ▼                │
                                                  │ LVGL 六页轮播 + 分层转场   │
                                                  └──────────────────────────┘
```

---

## 2. 代码结构

```
lvgl_template_project_PIO/
├── platformio.ini                  # 板卡、Arduino、ArduinoJson
├── boards/                         # 自定义 board json
├── lib/TFT_eSPI/User_Setup.h       # GC9A01 + 引脚 + 80 MHz
├── lib/lvgl/lv_conf.h              # LVGL 配置（关键项见 §7）
├── include/display.h
├── src/
│   ├── main.cpp                    # setup()/loop()
│   ├── display.cpp/.h              # LVGL 初始化、GC9A01 flush、双缓冲
│   ├── serial_reader.cpp/.h        # UART0 帧接收 + 共享 JSON 解析
│   ├── udp_reader.cpp/.h           # WiFi STA + UDP 广播接收
│   ├── net_config.h                # WiFi 账号 / UDP 端口（已 gitignore）
│   ├── net_config.example.h        # 配置模板（入库）
│   ├── CST816S.cpp/.h              # 触摸控制器驱动（已关自动休眠）
│   ├── touch.cpp/.h                # 20 ms 轮询、滑动/点击识别
│   ├── data_model.h                # pc_data_t：PC 原始数据
│   └── lv_monitor/
│       ├── monitor.c/.h            # 界面骨架、页面表、轮播、分层转场
│       ├── page.h                  # 页面注册表 monitor_page_t
│       ├── page_cpu.c / page_gpu.c / page_mem.c
│       ├── page_disk.c / page_net.c / page_sys.c
│       ├── common.c/.h             # 布局度量、页面骨架、图表、格式化
│       ├── sampler.c/.h            # pc_data_t → sys_sample_t + 60 s 历史
│       ├── theme.h                 # 颜色、时序、页数常量
│       └── font_cap_bold.c         # 10 px Montserrat Bold 子集
└── pc_companion/
    └── monitor.py                  # PC 数据采集 + UDP 广播 / 串口发送
```

---

## 3. 启动与主循环

### 3.1 `setup()`（`src/main.cpp`）

1. `setCpuFrequencyMhz(160)` — 从默认 240 MHz 降到 160 MHz，降低动态功耗（见 §7.1）。
2. `serial_reader_init(921600)` — 启动 UART0（GPIO43 TX / 44 RX），设置 2048 B 接收缓冲，等待 200 ms。
3. `udp_reader_init()` — `WiFi.mode(WIFI_STA)`、**开启 modem sleep**（射频在 DTIM 之间休眠）、开启自动重连，`WiFi.begin(SSID, PASS)`（非阻塞）。
4. `screen.init()` — `lv_init()`；TFT_eSPI 初始化并清屏（rotation 2 = 180°，见 §6.7）；背光引脚切到 LEDC PWM 并调暗；注册 LVGL 显示驱动与两块半屏缓冲。
5. `touch_init()` — CST816S 硬复位（RST 低 5 ms → 高 50 ms），读版本，**关闭自动休眠**，注册 LVGL 指针输入，创建 20 ms 轮询定时器。
6. `lv_monitor_set_pc_data(&pc_data)` — 把全局 `pc_data` 交给采样器。
7. `lv_monitor_show()` — `sampler_init()`、`mon_ui_init()`、`build_chrome()`，注册手势回调，创建 250 ms 轮播定时器和 1000 ms 数据刷新定时器。
8. 串口打印 Build/Ready（仅调试用）。

### 3.2 `loop()`

| 顺序 | 动作 |
|---|---|
| 1 | `serial_reader_poll(&pc_data)` — 收串口帧 |
| 2 | `udp_reader_poll(&pc_data)` — 收 UDP 数据报、维护 WiFi 重连 |
| 3 | 每 ≥5 ms 调一次 `lv_task_handler()`（LVGL 自己的刷新周期是 10 ms） |
| 4 | 每 2 s 向 Serial0 打印帧数 / FPS（无显示用途） |
| 5 | `delay(1)` |

### 3.3 运行时定时器

| 定时器 | 周期 | 归属 | 作用 |
|---|---|---|---|
| 触摸轮询 | 20 ms | `touch.cpp` | 读触摸点、识别滑动/点击 |
| 采样 | 1000 ms | `sampler.c` | 把最新 `pc_data_t` 平滑成 `sys_sample_t`，推进历史 |
| 页面数据刷新 | 1000 ms | `monitor.c` | 刷新当前页数值（转场期间跳过） |
| 轮播检查 | 250 ms | `monitor.c` | 距上次切页 ≥5 s 则自动前进一页 |
| 英雄数字补间 | 420 ms | `common.c` | 数值变化时缓动 |
| 磁盘条补间 | 650 ms | `page_disk.c` | 宽度变化缓动 |
| 页面转场 | 600 ms | `monitor.c` | 分层转场总时长 |

---

## 4. 通信逻辑

### 4.1 传输方式总览

| 通道 | 方向 | 默认 | 帧边界 | 用途 |
|---|---|---|---|---|
| WiFi + UDP 广播 | PC → ESP32 | 是 | 一个数据报 = 一个 JSON | 主通道，免 IP 配置 |
| USB 串口 UART0 | PC → ESP32 | 否 | `STX + JSON + ETX` | 备用/调试，仍完整保留 |

两条通道**可以同时工作**：谁先到达就更新同一份 `pc_data`，解析入口都是 `serial_reader_parse_json()`。

### 4.2 WiFi + UDP 广播（主通道）

PC 端（`monitor.py`）：

- 每个周期调用一次 `collect_all()`：1 s 固定间隔 + 约 0.6 s 采集耗时，实测约 **1.5 s 一帧**。整个 JSON 序列化为一个 UDP 数据报（实测约 **863 B**，远小于 MTU，不会分片）。
- 端口 **4210**，与设备端 `NET_UDP_PORT` 保持一致。
- 广播目标不是固定写死：`get_broadcast_targets()` 用 `psutil.net_if_addrs()` 枚举所有活跃 IPv4 接口，按 `IP | ~netmask` 计算各自的定向广播地址（如 `192.168.10.255`）；跳过回环和 169.254.*；一个都没有时退回 `255.255.255.255`。
- 每 10 帧（约 15 s）重新计算一次目标，适应 WiFi 开关、VPN、网卡插拔。
- 某个目标发送失败只跳过该目标，不影响其他网卡。

设备端（`udp_reader.cpp`）：

- `WIFI_STA` 模式连接 `net_config.h` 中配置的 2.4 GHz 网络。
- **开启 modem sleep**（`WiFi.setSleep(true)`）：射频在 DTIM 之间休眠，空闲功耗显著下降；代价是收包延迟增加约 100–300 ms，对 1.5 s 一帧的数据流无影响。
- 连接非阻塞：WiFi 未连上时 UI 照常运行；断开时关闭 UDP 套接字，并每 10 s 重新 `WiFi.begin()`。
- 连上后 `s_udp.begin(4210)`，`loop()` 中每个 `parsePacket()` 循环取走所有排队数据报。
- 接收缓冲 2048 B；超过缓冲的数据报整包丢弃并累加 `overflows`，避免解析截断的 JSON。
- 每个数据报直接交给共享解析函数，无 STX/ETX、无应用层确认（UDP 自带校验和，丢包由下一帧自然补偿）。

典型广播目标（本机）：

```
172.29.207.255   Hyper-V vEthernet
192.168.1.255    VMware VMnet8
192.168.10.255   有线主网段（设备所在 LAN）
```

### 4.3 串口（备用通道）

- `Serial0` = 硬件 UART0（GPIO43 TX / GPIO44 RX），波特率 **921600**，接板载 USB-UART。
- 帧格式：`[0x02][JSON][0x03]`；帧外的字节一律丢弃；缓冲区 2560 B，溢出计 `overflows`。
- 保留原因：WiFi 不可用时仍可通过 `python monitor.py COM7` 出图，同时也是烧录/诊断口。

### 4.4 JSON 数据帧

```jsonc
{
  "ts": 1760000000,                // Unix 时间戳（秒）
  "cpu": {
    "name": "AMD Ryzen ...",       // 注册表读取
    "usage": 23.4,                 // 0-100，浮点
    "temp": 45,                    // °C；-1 = 无传感器
    "freq": 3800,                  // MHz
    "cores": 16,                   // 逻辑核数
    "procs": 312                   // 进程数（无温度时顶替显示）
  },
  "gpu": {                         // 无 NVIDIA 卡时为 null
    "name": "NVIDIA ...",
    "usage": 12, "temp": 41,
    "mem_used": 2048, "mem_total": 8192,   // MB
    "power_w": 65,
    "core_clock": 1920, "mem_clock": 7001, // MHz
    "fan_speed": 30                        // %
  },
  "ram": { "used": 18320, "total": 32768,
           "swap_used": 1024, "swap_total": 9760 },   // MB
  "disk": [                         // 仅“固定”磁盘的已挂载卷
    { "name": "C:", "used": 812345, "total": 1953514, "temp": -1 }  // MB
  ],
  "disk_phys_total": 4338177,      // 所有物理盘总容量，MiB；0 = 不可用
  "net": {
    "up": 120, "down": 3400,       // KB/s（相对上次采样的增量）
    "total_up": 12345678, "total_down": 87654321  // 自开机累计 KB
  },
  "loadavg": [1.25, 0.80, 0.55],   // 1/5/15 分钟负载
  "uptime": 1234567                // 开机秒数
}
```

设备端解析要点（`serial_reader.cpp`）：

- `cpu.usage` 是浮点，ArduinoJson 不能 `| 0` 直接取整，先按 `float` 读入再四舍五入并限制 0–100。
- `gpu` 为 `null` 时 `gpu_present = 0`，GPU 页显示 `--`。
- `loadavg` 缺失时用 CPU 使用率近似 1/5/15 分钟负载。
- 任何来源的帧都会更新 `updated`、`frame_count`、`last_recv_ms`；解析失败累加 `parse_errs` 并保存前 64 字节原文。
- 解析使用静态 `JsonDocument`，避免堆碎片。

### 4.5 数据新鲜度

- `last_recv_ms` 由每次成功解析写入（串口与 UDP 共用）。
- 采样器判定“新鲜”：距最后一次成功接收 < **3000 ms**。
- 网络速率只在新鲜时更新；超过 3 s 未收到数据时，RX/TX 速率归零，不保留陈旧速率。
- CPU、内存、磁盘、GPU 等数值没有单独的新鲜度判断，链路断开后会保持最后一次的值。

### 4.6 PC 端采集细节（`pc_companion/monitor.py`）

| 数据 | 来源 |
|---|---|
| CPU 名称 | 注册表 `HARDWARE\DESCRIPTION\System\CentralProcessor\0` |
| CPU 使用率 | `psutil.cpu_percent(interval=0.5)` |
| CPU 温度 | LibreHardwareMonitor Web Server（`127.0.0.1:8085/data.json`），后台线程探测；正常时 10 s 刷新，不可用时 60 s 重试 |
| CPU 频率 / 核数 / 进程数 | psutil |
| GPU | `nvidia-smi` 查询温度/占用/显存/功耗/频率/风扇；无 NVIDIA 卡返回 `null` |
| 内存 / 交换 | `psutil.virtual_memory()` / `swap_memory()`，换算 MB |
| 卷容量 | `psutil.disk_partitions()`，仅保留 `opts` 含 `fixed` 的卷 |
| 物理盘总量 | PowerShell `Get-CimInstance Win32_DiskDrive` 求和，换算 MiB，进程内只查询一次 |
| 网络速率 | `psutil.net_io_counters()` 与上次采样做差分，除以时间得 KB/s |
| 网络累计 | 计数器自开机累计字节数，换算 KB |
| 负载 | `psutil.getloadavg()`（Windows 首次调用可能阻塞约 5 s） |
| 开机时长 | `time.time() - psutil.boot_time()` |

### 4.7 配置与安全

- WiFi SSID / 密码、UDP 端口在 `src/net_config.h`。
- 该文件已加入 `.gitignore`（仓库为公开 GitHub 仓库），入库的是占位模板 `src/net_config.example.h`。
- 端口 4210 在 `net_config.h` 与 `monitor.py` 各出现一次，修改时必须同步。

---

## 5. 采样与数据模型

### 5.1 两层结构

| 结构 | 文件 | 内容 |
|---|---|---|
| `pc_data_t` | `src/data_model.h` | PC 原始帧：CPU/GPU/RAM/磁盘数组/网络/负载/诊断计数 |
| `sys_sample_t` | `src/lv_monitor/sampler.h` | UI 直接使用的平滑值 + 6 条 60 点历史 + 环形写指针 |

### 5.2 采样器行为（`sampler.c`，每秒一次）

- **EMA 平滑**：CPU、GPU 使用率按 `(旧×3 + 新×2) / 5` 平滑，抑制跳变。
- **网络**：对 B/s 做同样的 EMA；PC 提供累计值时用累计值，否则设备端按速率累加。
- **磁盘**：每第 5 个采样（约 5 s）才重算一次，避免频繁读盘数据抖动：
  1. 汇总所有已挂载固定卷的 used/total；
  2. `avail = total - used`；
  3. 若 PC 上报的 `disk_phys_total_mb > total`，把总容量换成物理盘总量，`used = total - avail`（隐藏分区、恢复分区、未分配空间都算“已用”）；
  4. 计算占用百分比、KB 值。
- **历史**：首次采样先“铺满”初值，之后每秒写入环形缓冲，保留最近 60 秒；图表按 `hist_head` 展开为从旧到新的顺序。
- **GPU 无效**（无卡 / 数据为空）时清零所有 GPU 字段。

---

## 6. 显示逻辑

### 6.1 屏幕骨架（`build_chrome()`）

- 屏幕底色纯黑 `MON_BG = 0x000000`。
- 表圈：直径 `d + 2` 的圆，颜色 `0x0D131A`，只露出一圈极暗的收边。
- 表盘：直径 `d = 短边 - 16`（本机 224 px）的裁剪圆，**纯黑**（不做近黑渐变，原因见注释：RGB565 下会分层或出现 8×8 抖动噪点）。
- `s_content`：与表盘同尺寸的透明裁剪容器，承载 6 个页面根对象。
- 6 个页面在启动时**一次性全部创建**并隐藏，切页只切换可见性和样式，不重建对象。
- 页面圆点对象 `s_dots` 叠在内容之上、靠近表盘底部。

### 6.2 页面通用骨架（`mon_page_ui_create()`）

每个页面由同一套部件组成，位置按表盘直径的千分比计算：

| 部件 | 位置 | 内容 |
|---|---|---|
| `tag_row` | 13% 高度 | 强调色小圆点 + 大写页面名（CPU / GPU / MEMORY / STORAGE / NETWORK / SYSTEM） |
| `hero_box` | 40% 高度 | 主数值 + 单位，水平居中；GPU 页无卡时显示 `--` |
| `stage` | 63% 高度，72% 宽 | 折线图或容量条 |
| `sub_row` | 79.5% 高度，74% 宽 | 两个子统计（值 + 说明），中间一条细分隔线 |
| `note` | 图表左上角 | 小徽标（如 `60s`、`MAX 4.0 MB/s`）；紧凑屏多数隐藏 |
| `dots` | 90.5% 高度 | 6 个页面圆点 |

本机 `d = 224`（compact），字体选择：

- 主数值 Montserrat 36，单位 18，页面名 16（保证远距离可读），子数值 16；
- 子标题使用自制 10 px Montserrat Bold 子集 `font_cap_bold`；
- SYSTEM 页主数值为 7–8 字符的时长文本，单独降为 Montserrat 26，避免溢出。

### 6.3 六个页面（顺序即轮播顺序）

| # | 页面 | 强调色 | 主数值 | 图表区 | 子统计 1 | 子统计 2 | 备注 |
|---|---|---|---|---|---|---|---|
| 1 | CPU | `0xFF7A59` 珊瑚橙 | CPU 占用 % | CPU 60 s 折线 | TEMP（无温度时显示 PROC 进程数） | FREQ | — |
| 2 | GPU | `0xFF6FA5` 玫瑰粉 | GPU 占用 % | GPU 60 s 折线 | TEMP | VRAM 已用/总量 | 无卡时 `--` |
| 3 | MEMORY | `0x5B9DFF` 电光蓝 | 内存占用 % | 内存 60 s 折线 | USED 已用/总量 | SWAP 已用（无交换显示 OFF） | — |
| 4 | STORAGE | `0xFFC857` 琥珀黄 | 磁盘占用 % | 容量条（25/50/75% 刻度） | USED 已用/总容量 | FREE 可用 | 十进制单位 |
| 5 | NETWORK | `0x4FE3C1` 薄荷绿 | 下载速率 | RX/TX 双线，自动量程 | UPLOAD 上传速率 | TOTAL 累计流量 | 右上角 MAX 徽标（非紧凑屏） |
| 6 | SYSTEM | `0xB98CFF` 鸢尾紫 | 开机时长（26 px） | 1 分钟负载 60 s 折线 | LOAD 5M | LOAD 15M | 徽标显示 1M 负载（非紧凑屏） |

细节：

- **CPU 温度**：Windows 无驱动无法直接读，未启动 LibreHardwareMonitor 时改用进程数显示，标题为 `PROC`。
- **STORAGE**：容量条由轨道（白色 10% 透明度）、渐变填充（强调色→偏白，40% 对比度）、顶部高光条和 25/50/75% 三条刻度组成；填充宽度 650 ms 缓动。
- **NETWORK**：量程取 60 s 峰值的 2 的幂（最小 64 KB/s），两条曲线使用不同颜色（RX 强调色，TX `0x7C8DFF`）；面积渐变只填充第一条曲线。
- **SYSTEM**：负载折线的上限取峰值 +25%，且不低于 1.0。

### 6.4 数值格式化（`common.c`）

| 场景 | 规则 |
|---|---|
| 通用数字 | ≥100 取整，否则 1 位小数 |
| 内存 / 网络 / GPU 显存 | 二进制单位：1024 进制，K/M/G/T |
| **磁盘容量** | **十进制**：1 TB = 1000⁴ 字节，与硬盘标签一致（2T+2T+500G 显示 4.5T） |
| 网络速率 | B/s、KB/s、MB/s、GB/s，紧凑模式用 `K/s`、`M/s` |
| 频率 | ≥1 GHz 显示 GHz，否则 MHz；0 显示 `--` |
| 开机时长 | 有天数 `Xd Yh`；否则 `Xh Ym`；不足 1 h 显示 `Xm` |
| 负载 | 固定两位小数 |

### 6.5 动效

**主数值补间**：数值变化时用 420 ms ease-out 从旧值过渡到新值，更新频率限制在约 30 fps；差值 < 0.001 直接设置，避免无意义动画。

**图表面积**：在折线下方绘制一层带蒙版渐变的填充（40% 透明度），渐变锚点跟随该段曲线自身高度，低负载时也可见；双序列图表只填充第一条，避免颜色浑浊。

**分层转场（600 ms）**：`start_transition(dir)` 同时隐藏切换 6 页中的相邻两页。

| 元素 | 退出页 | 进入页 |
|---|---|---|
| 主数值 | 0–43%：zoom 256→64，透明度 255→0，ease-in | 52–100%：zoom 64→256，透明度 0→255，ease-out |
| 子统计行 | 同 0–43%：向下平移 0→84 px | 52–100%：从 84 px 归位 |
| 图表 + 徽标 | 0–48%：淡出并上移 6 px | 40–88%：淡入并从 +6 px 归位 |
| 页面名 | 0–43%：淡出 + ±1 px 抖动，字符乱码逐渐解出 | 52–100%：淡入 + 抖动，乱码逐渐解码为真名 |
| 页面圆点 | 全 600 ms：胶囊从旧位置滑到新位置（ease-in-out）；颜色在绘制时由位置推算，天然与移动同步 |

转场结束回调 `layer_ready()` 会：

1. 把两页所有被动画改过的样式复位（透明度、zoom、位移、文字）；
2. 隐藏退出页、显示进入页；
3. 更新当前页索引与停留计时；
4. 解除 `s_transitioning`；
5. 圆点位置落到精确值（颜色由位置推算，无需单独收尾）；
6. 立即用最新采样刷新新页。

> **关键配置**：`LV_COLOR_SCREEN_TRANSP` 必须为 **1**。LVGL v8 对带透明度或缩放的图层调用 `lv_draw_sw_layer_create()`，该宏为 0 时直接返回 NULL，动画对象根本不会被绘制——表现就是转场期间整屏黑、结束瞬间硬切。

### 6.6 页面圆点（替代原刻度环）

- 6 个静态白点：步距 `max(8, d×46/1000)`（本机 10 px），直径 `max(3, d/90)`（本机 3 px），透明度 64。
- 当前页由一枚强调色胶囊表示：宽 `max(8, 步距×4/5)`（本机 8 px），高 `点径+2`，带一圈 40 透明度的光晕。
- 绘制方式：`LV_EVENT_DRAW_MAIN_BEGIN` 自绘回调，直接用 `lv_draw_rect` 画圆和胶囊，不创建子对象。
- **颜色与位置同源**：胶囊颜色不单独维护，而是在绘制回调里按 `t = (s_dot_pos - from) / (to - from)` 对两个页面的强调色做 `lv_color_mix(新色, 旧色, t×255)`；位置由 `layer_anim_cb` 用同一条 ease-in-out 曲线驱动，因此颜色不可能超前或滞后。
- 位置变化时只 `lv_obj_invalidate()`，不重排布局。

### 6.7 触摸与自动轮播

- CST816S 每 20 ms 轮询一次；丢弃多触点、坐标越界、I2C 读取失败的样本。
- 按下记录起点，抬起时判定：
  - 水平位移 ≥ 30 px 且大于垂直位移 → 左/右滑；
  - 位移 ≤ 10 px 且按下时长 ≤ 600 ms → 点击（当前不用于翻页）；
  - 两次触摸采样擦除/短于 2 个样本的“幽灵触摸”被忽略。
- 左滑前进一页，右滑后退一页；**转场期间（600 ms）忽略新的滑动**。
- 自动轮播：每页停留 5 s，前进一页；用户滑动会重置 5 s 计时。
- **自动休眠已关闭**：CST816S 的 `0xFE` 寄存器 bit0 在复位后置 1。默认 2 s 无触摸即休眠，唤醒它的第一次触摸会被吞掉——这正是“刚烧录完第一下滑动无反应”的原因。关闭后待机电流增加约 1.6 mA。
- **180° 安装**：显示用 `tft.setRotation(2)`；触摸在 `touch_poll_cb` 里把原始坐标镜像为 `x = 239 - x, y = 239 - y`，与显示共用同一视觉坐标系，滑动的左右方向保持所见即所得。

---

## 7. 关键配置

### 7.1 功耗策略

| 措施 | 位置 | 效果 | 代价 |
|---|---|---|---|
| WiFi modem sleep | `src/udp_reader.cpp` | 射频在 DTIM 之间休眠，空闲功耗降幅最大 | 收包延迟 +100–300 ms |
| CPU 240 → 160 MHz | `src/main.cpp` | SoC 动态功耗下降 | 转场动画帧率略降 |
| 背光 PWM 160/255 | `src/display.cpp`（LEDC 5 kHz，`TFT_BL`=GPIO2） | 背光电流约降三分之一，是第二大热源 | 屏幕变暗；`BL_LEVEL` 调到 255 即全亮 |
| CST816S 关闭自动休眠 | `src/CST816S.cpp` | 换回“第一下滑动即响应” | 待机电流 +约 1.6 mA |

### 7.2 关键配置

| 文件 | 配置 | 值 / 说明 |
|---|---|---|
| `src/lv_monitor/theme.h` | `MON_PAGE_MS` | 5000 ms，每页停留 |
| | `MON_TRANS_MS` | 600 ms，转场时长 |
| | `MON_HIST_LEN` | 60，历史点数 |
| | `MON_PAGE_COUNT` | 6 |
| | 颜色常量 | 背景/表圈/文字/六个强调色 |
| `lib/lvgl/lv_conf.h` | `LV_COLOR_DEPTH` | 16 |
| | `LV_COLOR_SCREEN_TRANSP` | **1**（分层动画必需） |
| | `LV_MEM_CUSTOM` | 1（使用 malloc/free） |
| | `LV_DISP_DEF_REFR_PERIOD` | 10 ms |
| | `LV_INDEV_DEF_READ_PERIOD` | 30 ms |
| `src/net_config.h` | `NET_WIFI_SSID` / `NET_WIFI_PASS` | 2.4 GHz 网络；文件不入库 |
| | `NET_UDP_PORT` | 4210 |
| `pc_companion/monitor.py` | `UDP_PORT` | 4210，需与固件一致 |
| | `INTERVAL` | 1.0 s 发送间隔（加采集耗时，实际约 1.5 s/帧） |
| | `BAUD_RATE` | 921600（串口备用） |
| `lib/TFT_eSPI/User_Setup.h` | `GC9A01_DRIVER` / `TFT_HEIGHT` | 240×240 |
| | `SPI_FREQUENCY` | 80 MHz |

资源占用（WiFi 版本实测）：静态 RAM 55.9%（183 KB / 320 KB），Flash 22.3%（1.46 MB / 6.55 MB）。

---

## 8. 构建、烧录与运行

### 8.1 固件

```bash
# 编译
"$HOME/.platformio/penv/Scripts/platformio.exe" run -d <项目目录>

# 烧录（COM7 被占用时先停掉 monitor.py）
"$HOME/.platformio/penv/Scripts/platformio.exe" run -d <项目目录> -t upload
```

或在 VS Code 的 PlatformIO 工具栏点击 Build / Upload。

### 8.2 PC 端

```bash
cd pc_companion

python monitor.py               # WiFi UDP 广播（默认）
python monitor.py COM7          # 串口模式（备用）
python monitor.py --udp         # 强制 UDP
python monitor.py --udp-port 4210
python monitor.py --dry-run     # 只打印 JSON，不发送
python monitor.py --list        # 列出串口
```

启动时会打印实际广播目标，例如：

```
Broadcasting UDP on port 4210 to: 172.29.207.255, 192.168.1.255, 192.168.10.255
```

无窗口常驻（推荐）：双击 `pc_companion/start_monitor.vbs`，它用 `pythonw.exe` 静默启动，不占任务栏。`monitor.py` 对 `nvidia-smi` 和 PowerShell 的调用都加了 `CREATE_NO_WINDOW`，否则无控制台运行时会每秒闪出一个临时窗口。静默模式下输出被丢弃；要看实时数值时用上面的 `python monitor.py` 前台运行。

### 8.3 首次配置 WiFi

1. 复制 `src/net_config.example.h` 为 `src/net_config.h`（若不存在）；
2. 填入 2.4 GHz 网络的 SSID / 密码；
3. 重新编译并烧录。

---

## 9. 已知限制与注意事项

- **UDP 无确认、无重传**：单帧丢失由下一帧自然补齐；约 1.5 s 的刷新节奏下无感知。设备不向 PC 回传任何数据。
- **广播不跨网段**：PC 与 ESP32 必须在同一 LAN（例如都接在 192.168.10.0/24 路由器下）；路由器若开启 AP 隔离/客户端隔离，有线到无线的广播可能被挡住。
- **仅 2.4 GHz**：SSID 必须是 2.4 GHz 网络，ESP32-S3 无 5 GHz 射频。
- **多播源**：如果有多个 PC 同时广播到 4210，帧会交替到达，界面数值会来回跳。
- **CPU 温度依赖 LHM**：未运行 LibreHardwareMonitor（管理员、开启 Remote Web Server 8085）时，CPU 页温度位置显示进程数。
- **磁盘总量口径**：`disk_phys_total` 来自物理盘总和，隐藏/恢复分区与未分配空间计入“已用”，因此页面总量会高于“我的电脑”里各卷之和；格式化使用十进制 TB，与硬盘标称一致。
- **静态 RAM 已过半**：加入 WiFi 协议栈后静态占用约 56%，后续增加大缓冲或对象时注意堆余量。
- **凭据管理**：`src/net_config.h` 含明文 WiFi 密码且已被 git 忽略；`src/net_config.example.h` 才是入库模板，不要搞反。

---

## 10. 故障排查

| 现象 | 可能原因 | 处理 |
|---|---|---|
| 表盘一直停在 0 / 不刷新（UDP 模式） | WiFi 未连上、SSID 为 5 GHz、密码错、网段不同、AP 隔离 | 看路由是否分配了 IP；临时用 `python monitor.py COM7` 验证界面；确认广播目标包含设备所在网段 |
| 刚烧录后第一下滑动无效 | CST816S 自动休眠（旧固件） | 当前固件已通过 `0xFE` 关闭自动休眠；若仍出现，检查 `CST816S::begin()` 的 `setAutoSleep(false)` |
| 转场时黑屏、结束瞬间硬切 | `LV_COLOR_SCREEN_TRANSP` 为 0 | 置 1 并重新编译烧录 |
| 烧录报错 “COM port busy” | `monitor.py COM7` 或串口监视器占用 | 先停止占用进程；UDP 模式不占用串口 |
| GPU 页显示 `--` | 无 NVIDIA 显卡 / `nvidia-smi` 不在 PATH | 正常降级；安装驱动或忽略 |
| CPU 温度显示 PROC 和进程数 | 未运行 LibreHardwareMonitor | 以管理员启动 LHM，开启 Options → Remote Web Server |
| 磁盘显示 4.0T 而不是 4.5T | 配对端是旧版 monitor.py，未发送 `disk_phys_total` | 重启 PC 端到当前版本（该值只查询一次） |
| WiFi 连不上且串口也不出图 | 供电/固件异常 | 重新烧录；用串口模式确认主程序运行 |

---

*文档对应代码状态：WiFi/UDP 双通道 + 页面圆点（位置推导颜色）+ 分层转场 + 180° 旋转 + 降功耗策略 + 十进制磁盘容量。*
