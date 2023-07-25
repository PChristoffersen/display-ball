#include "projectconfig.h"

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "app_base.h"
#include "display.h"
#include "wifi.h"
#include "input.h"
#include "console.h"

static constexpr char TAG[] = "main";




static void draw_event_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part == LV_PART_ITEMS) {
        lv_obj_t *obj = lv_event_get_target(e);
        lv_chart_series_t *ser = lv_chart_get_series_next(obj, NULL);
        uint32_t cnt = lv_chart_get_point_count(obj);
        /*Make older value more transparent*/
        dsc->rect_dsc->bg_opa = (LV_OPA_COVER *  dsc->id) / (cnt - 1);

        /*Make smaller values blue, higher values red*/
        lv_coord_t *x_array = lv_chart_get_x_array(obj, ser);
        lv_coord_t *y_array = lv_chart_get_y_array(obj, ser);
        /*dsc->id is the tells drawing order, but we need the ID of the point being drawn.*/
        uint32_t start_point = lv_chart_get_x_start_point(obj, ser);
        uint32_t p_act = (start_point + dsc->id) % cnt; /*Consider start point to get the index of the array*/
        lv_opa_t x_opa = (x_array[p_act] * LV_OPA_50) / 200;
        lv_opa_t y_opa = (y_array[p_act] * LV_OPA_50) / 1000;

        dsc->rect_dsc->bg_color = lv_color_mix(lv_palette_main(LV_PALETTE_RED),
                                               lv_palette_main(LV_PALETTE_BLUE),
                                               x_opa + y_opa);
    }
}

static void add_data(lv_timer_t *timer)
{
    lv_obj_t *chart = static_cast<lv_obj_t*>(timer->user_data);
    lv_chart_set_next_value2(chart, lv_chart_get_series_next(chart, NULL), lv_rand(0, 200), lv_rand(0, 1000));
}

void example_lvgl_demo_ui()
{
    auto disp = lv_disp_get_default();

    lv_theme_t *theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, theme);

    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_t *chart = lv_chart_create(scr);
    lv_obj_set_size(chart, 150, 150);
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(chart, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);   /*Remove the lines*/

    lv_chart_set_type(chart, LV_CHART_TYPE_SCATTER);

    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 5, 5, 5, 1, true, 30);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 10, 5, 6, 5, true, 50);

    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, 200);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);

    lv_chart_set_point_count(chart, 50);

    lv_chart_series_t *ser = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < 50; i++) {
        lv_chart_set_next_value2(chart, ser, lv_rand(0, 200), lv_rand(0, 1000));
    }

    lv_timer_create(add_data, 100, chart);
}


static void on_input(void* arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    input_event_data_t *data = static_cast<input_event_data_t*>(event_data);
    switch (event_id) {
        case EVENT_BUTTON_LEFT:
            ESP_LOGI(TAG, "LEFT Button %s", data->long_press?"long":"short");
            break;
        case EVENT_BUTTON_RIGHT:
            ESP_LOGI(TAG, "RIGHT Button %s", data->long_press?"long":"short");
            if (!data->long_press) {
                if (!display_get_backlight()) {
                    display_set_backlight(true);
                }
            }
            else {
                ESP_LOGI(TAG, "Toggle backlight");
                display_set_backlight(!display_get_backlight());
            }
            break;
    }
}



extern "C" void app_main() 
{
    app_base_init();
    display_init();
    wifi_init();
    input_init();


    ESP_LOGI(TAG, "Display init");
    display_acquire();
    example_lvgl_demo_ui();
    display_release();

    console_init();

    ESP_LOGI(TAG, "Running");

    app_event_handler_register(INPUT_EVENT, ESP_EVENT_ANY_ID, on_input, nullptr);

    while (true) {
        app_event_loop_run(pdMS_TO_TICKS(1000));
        vTaskDelay(1);
    }
}
