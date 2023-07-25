#pragma once

#include <freertos/FreeRTOS.h>
#include <esp_event.h>

ESP_EVENT_DECLARE_BASE(INPUT_EVENT);

enum input_event_t {
    EVENT_BUTTON_LEFT,
    EVENT_BUTTON_RIGHT
};

struct input_event_data_t {
    TickType_t ts;
    bool long_press;
};

void input_init();
