#include "input.h"

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/touch_pad.h>
#include <esp_log.h>

#include "projectconfig.h"
#include "app_base.h"

static constexpr char TAG[] = "touch";

static constexpr auto TOUCH_BUTTON_COUNT { 2u };
static constexpr touch_pad_t TOUCH_BUTTON[TOUCH_BUTTON_COUNT] = {
    TOUCH_PAD_NUM2,
    TOUCH_PAD_NUM1,
};

static constexpr uint32_t TOUCH_PRESSED_THRESHOLD { 35000 };
static constexpr TickType_t TOUCH_DEBOUNCE_DELAY { pdMS_TO_TICKS(100) };
static constexpr TickType_t TOUCH_LONG_PRESS { pdMS_TO_TICKS(1000) };

static constexpr uint32_t INPUT_TASK_STACK_SIZE { 4096 };


static TaskHandle_t g_input_task = nullptr;


ESP_EVENT_DEFINE_BASE(INPUT_EVENT);


static void input_task(__unused void *param)
{
    ESP_LOGI(TAG, "Touch init");

    vTaskDelay(pdMS_TO_TICKS(1000));

    touch_pad_init();
    //touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    for (int i = 0; i < TOUCH_BUTTON_COUNT; i++) {
        touch_pad_config(TOUCH_BUTTON[i]);
    }

    static constexpr touch_pad_denoise_t denoise = {
        /* The bits to be cancelled are determined according to the noise level. */
        .grade = TOUCH_PAD_DENOISE_BIT4,
        .cap_level = TOUCH_PAD_DENOISE_CAP_L4,
    };
    touch_pad_denoise_set_config(&denoise);
    touch_pad_denoise_enable();

    /* Enable touch sensor clock. Work mode is "timer trigger". */
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();

    /* Wait touch sensor init done */
    vTaskDelay(pdMS_TO_TICKS(250));

    static struct {
        TickType_t change;
        bool pressed;
        bool long_press;
    } button_state[TOUCH_BUTTON_COUNT];

    for (uint i=0; i<TOUCH_BUTTON_COUNT; i++) {
        button_state[i].change = 0;
        button_state[i].pressed = false;
    }

    uint32_t counter = 0;
    uint32_t touch_value;
    while (true) {
        counter++;
        auto now = xTaskGetTickCount();
        for (uint i = 0; i < TOUCH_BUTTON_COUNT; i++) {
            auto &current = button_state[i];

            if (now-current.change < TOUCH_DEBOUNCE_DELAY)
                continue;

            touch_pad_read_raw_data(TOUCH_BUTTON[i], &touch_value);    // read raw data.

            bool pressed = touch_value>TOUCH_PRESSED_THRESHOLD;

            if (pressed) {
                if (!current.pressed) {
                    current.change = now;
                    current.pressed = true;
                    current.long_press = false;
                }
                else if (!current.long_press && (now-current.change > TOUCH_LONG_PRESS)) {
                        ESP_LOGD(TAG, "[%d] Long press", i);
                        input_event_data_t data = {
                            .ts = now,
                            .long_press = true,
                        };
                        app_event_post(INPUT_EVENT, i, &data, sizeof(data), 0);
                        current.long_press = true;
                }
            }
            else if (current.pressed) { // Released
                if (!current.long_press) {
                    ESP_LOGD(TAG, "[%d] Short press", i);
                        input_event_data_t data = {
                            .ts = now,
                            .long_press = false,
                        };
                        app_event_post(INPUT_EVENT, i, &data, sizeof(data), 0);
                }
                ESP_LOGD(TAG, "[%d] Released %lu ms", i, pdTICKS_TO_MS(now-current.change));
                current.pressed = false;
                current.long_press = false;
                current.change = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


void input_init()
{
    static StaticTask_t task_buffer;
    static StackType_t task_stack[INPUT_TASK_STACK_SIZE];
    g_input_task = xTaskCreateStatic(input_task, "touch_input", INPUT_TASK_STACK_SIZE, nullptr, INPUT_TASK_PRIORITY, task_stack, &task_buffer);
}

