#ifndef __MONITOR_PAGE_H
#define __MONITOR_PAGE_H

#include "lvgl.h"
#include "sampler.h"

typedef struct {
    uint32_t accent_hex;
    lv_obj_t *(*create)(lv_obj_t *parent, lv_color_t accent);
    void (*update)(const sys_sample_t *sample);
    lv_obj_t *root;
} monitor_page_t;

lv_obj_t *page_cpu_create(lv_obj_t *parent, lv_color_t accent);
void page_cpu_update(const sys_sample_t *sample);

lv_obj_t *page_mem_create(lv_obj_t *parent, lv_color_t accent);
void page_mem_update(const sys_sample_t *sample);

lv_obj_t *page_disk_create(lv_obj_t *parent, lv_color_t accent);
void page_disk_update(const sys_sample_t *sample);

lv_obj_t *page_net_create(lv_obj_t *parent, lv_color_t accent);
void page_net_update(const sys_sample_t *sample);

lv_obj_t *page_sys_create(lv_obj_t *parent, lv_color_t accent);
void page_sys_update(const sys_sample_t *sample);

lv_obj_t *page_gpu_create(lv_obj_t *parent, lv_color_t accent);
void page_gpu_update(const sys_sample_t *sample);

#endif /* __MONITOR_PAGE_H */
