#include "input.h"

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/touch_pad.h>
#include <esp_log.h>
#include <lvgl.h>


#include "projectconfig.h"
#include "app_base.h"

static constexpr char TAG[] = "touch";

static constexpr auto TOUCH_BUTTON_COUNT { 2u };
static constexpr touch_pad_t TOUCH_BUTTON[TOUCH_BUTTON_COUNT] = {
    TOUCH_PAD_NUM2,
    TOUCH_PAD_NUM1,
};

static constexpr uint32_t TOUCH_PRESSED_THRESHOLD { 32000 };
static constexpr TickType_t TOUCH_DEBOUNCE_DELAY { pdMS_TO_TICKS(100) };
static constexpr TickType_t TOUCH_LONG_PRESS { pdMS_TO_TICKS(1000) };

static constexpr uint32_t INPUT_TASK_STACK_SIZE { 4096 };
static constexpr uint32_t INPUT_QUEUE_SIZE { 8 };


struct input_event_t {
    uint pad;
    bool pressed;
};


static void input_task(__unused void *param)
{
    QueueHandle_t queue = static_cast<QueueHandle_t>(param);

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

            if (pressed!=current.pressed) {
                current.pressed = pressed;
                current.change = now;

                input_event_t event = {
                    .pad = i,
                    .pressed = pressed
                };
                xQueueSend(queue, &event, portMAX_DELAY);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


static void input_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    QueueHandle_t queue = static_cast<QueueHandle_t>(indev_drv->user_data);

    input_event_t event;
    if (xQueueReceive(queue, &event, 0)) {
        data->state = event.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        switch (event.pad) {
            case 0: // Left
                ESP_LOGI(TAG, "Left %s", event.pressed?"pressed": "released");
                data->btn_id = event.pad;
                break;
            case 1: // Right
                ESP_LOGI(TAG, "Right %s", event.pressed?"pressed": "released");
                data->btn_id = event.pad;
                break;
        }
        
        data->continue_reading = uxQueueMessagesWaiting(queue) > 0;
    }
}


void input_init()
{
    static QueueHandle_t queue = nullptr;
    if (!queue) {
        static StaticQueue_t queue_buffer;
        static uint8_t queue_data[sizeof(input_event_t)*INPUT_QUEUE_SIZE];
        queue = xQueueCreateStatic(INPUT_QUEUE_SIZE, sizeof(input_event_t), queue_data, &queue_buffer);
    }

    static lv_indev_t *indev = nullptr;
    if (!indev) {
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_BUTTON;
        indev_drv.read_cb = &input_read;
        indev_drv.user_data = queue;
        indev = lv_indev_drv_register(&indev_drv);

        /*Assign buttons to points on the screen*/
        static const lv_point_t btn_points[2] = {
            {     10, 10 }, 
            { 240-10, 10 },
        };
        lv_indev_set_button_points(indev, btn_points);
    }

    static TaskHandle_t task = nullptr;
    if (!task) {
        static StaticTask_t task_buffer;
        static StackType_t task_stack[INPUT_TASK_STACK_SIZE];
        task = xTaskCreateStatic(input_task, "touch_input", INPUT_TASK_STACK_SIZE, queue, INPUT_TASK_PRIORITY, task_stack, &task_buffer);
    }
}

