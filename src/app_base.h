#pragma once

#include <esp_event.h>

void app_base_init();

/**
 * Time config 
 * 
 * CET = CET-1CEST,M3.5.0,M10.5.0/3
 */
bool app_set_timezone(const char *tz);
bool app_set_ntp_server(const char *ntp_server);


/**
 * Application event loop
 */
esp_err_t app_event_loop_run(TickType_t ticks_to_run);

esp_err_t app_event_handler_register(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg);

esp_err_t app_event_post(esp_event_base_t event_base, int32_t event_id, const void *event_data, size_t event_data_size, TickType_t ticks_to_wait);
