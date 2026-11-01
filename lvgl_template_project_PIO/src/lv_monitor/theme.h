#ifndef __MONITOR_THEME_H
#define __MONITOR_THEME_H

/* ───────────── 时间 ───────────── */
#define MON_PAGE_MS        5000U   /* 自动转场间隔（每页停留 5 秒） */
#define MON_TRANS_MS       600U    /* 分层转场总时长 */

/* ───────────── 数据 ───────────── */
#define MON_HIST_LEN       60      /* 60 个采样点 = 60 秒 */
#define MON_PAGE_COUNT     6

/* ───────────── 颜色 ───────────── */
/* 深色区域一律用纯黑：RGB565 下接近黑色的渐变只有几级灰阶，
 * 不抖动就是分层，抖动补间又会变成 8x8 的噪点。 */
#define MON_BG             0x000000   /* 屏幕底色 */
#define MON_BEZEL          0x0D131A   /* 表圈收边，一圈很暗的细环 */
#define MON_DIAL           0x000000   /* 表盘盘面 */

#define MON_TEXT           0xF3F6FA
#define MON_TEXT_DIM       0x8793A5
#define MON_TEXT_FAINT     0x4E5A6A
#define MON_RING           0x2A3542
#define MON_RING_BRIGHT    0x3C4A5B

#define MON_ACCENT_CPU     0xFF7A59   /* 珊瑚橙 */
#define MON_ACCENT_MEM     0x5B9DFF   /* 电光蓝 */
#define MON_ACCENT_DISK    0xFFC857   /* 琥珀黄 */
#define MON_ACCENT_NET     0x4FE3C1   /* 薄荷绿 */
#define MON_ACCENT_SYS     0xB98CFF   /* 鸢尾紫 */
#define MON_ACCENT_GPU     0xFF6FA5   /* 玫瑰粉 */

#endif /* __MONITOR_THEME_H */
