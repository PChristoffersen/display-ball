#pragma once

#include <freertos/FreeRTOS.h>
#include <lvgl.h>

lv_disp_t *display_init();
lv_disp_t *display_get();

void display_set_backlight(bool enable);
bool display_get_backlight();

bool display_acquire(TickType_t ticksToWait = portMAX_DELAY);
void display_release();
