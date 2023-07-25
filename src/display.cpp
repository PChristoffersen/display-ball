#include "display.h"

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_gc9a01.h>

#include "projectconfig.h"

static constexpr char TAG[] = "display";

static constexpr gpio_num_t LCD_PIN_BK  { GPIO_NUM_4 }; // Backlight
static constexpr gpio_num_t LCD_PIN_RST { GPIO_NUM_5 }; // Reset
static constexpr gpio_num_t LCD_PIN_DC  { GPIO_NUM_6 }; // Data command control pin
static constexpr gpio_num_t LCD_PIN_CS  { GPIO_NUM_44 }; // Chip select
static constexpr gpio_num_t LCD_PIN_DIN { GPIO_NUM_9 };  // MOSI
static constexpr gpio_num_t LCD_PIN_MISO { GPIO_NUM_8 };  // MISO
static constexpr gpio_num_t LCD_PIN_CLK { GPIO_NUM_7 }; // SCK

static constexpr uint LCD_H_RES { 240 };
static constexpr uint LCD_V_RES { 240 };
static constexpr uint LCD_PIXEL_CLOCK_HZ { 20*1000*1000 };
static constexpr uint LCD_CMD_BITS { 8 };
static constexpr uint LCD_PARAM_BITS { 8 };
static constexpr uint32_t LCD_BK_LIGHT_ON_LEVEL { 1 };
static constexpr uint32_t LCD_BK_LIGHT_OFF_LEVEL { !LCD_BK_LIGHT_ON_LEVEL };

static constexpr uint LVGL_DRAW_BUFFER_ROWS { 40 };
static constexpr uint LVGL_TICK_PERIOD_MS { 2 };
static constexpr uint32_t LVGL_TASK_STACK_DEPTH { 8192 };


static lv_disp_t *g_display = nullptr;
static SemaphoreHandle_t g_display_sem = nullptr;
static TaskHandle_t g_display_task = nullptr;
static bool g_backlight = true;

static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = static_cast<lv_disp_drv_t*>(user_ctx);
    lv_disp_flush_ready(disp_driver);
    return false;
}


static void on_lvgl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = static_cast<esp_lcd_panel_handle_t>(drv->user_data);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}


static void on_lvgl_drv_update(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = static_cast<esp_lcd_panel_handle_t>(drv->user_data);

    ESP_LOGI(TAG, "Drv Update %d", (int)drv->rotated);
    switch (drv->rotated) {
    case LV_DISP_ROT_NONE:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    case LV_DISP_ROT_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISP_ROT_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISP_ROT_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    }
}


static void on_lvgl_tick(__unused void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}



static void display_task(void *param) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10));
        
        if (display_acquire(pdMS_TO_TICKS(10))) {
            lv_timer_handler();
            display_release();
        }
    }
}


lv_disp_t *display_init()
{
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    static lv_disp_drv_t disp_drv;      // contains callback functions

    ESP_LOGI(TAG, "Turn off LCD backlight");
    static constexpr gpio_config_t bk_gpio_config = {
        .pin_bit_mask = 1ULL << LCD_PIN_BK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(LCD_PIN_BK, LCD_BK_LIGHT_OFF_LEVEL);

    ESP_LOGI(TAG, "Initialize SPI bus");
    static spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_PIN_DIN,
        .miso_io_num = LCD_PIN_MISO,
        .sclk_io_num = LCD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,     ///< GPIO pin for spi data4 signal in octal mode, or -1 if not used.
        .data5_io_num = -1,     ///< GPIO pin for spi data5 signal in octal mode, or -1 if not used.
        .data6_io_num = -1,     ///< GPIO pin for spi data6 signal in octal mode, or -1 if not used.
        .data7_io_num = -1,     ///< GPIO pin for spi data7 signal in octal mode, or -1 if not used.
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),  ///< Maximum transfer size, in bytes. Defaults to 4092 if 0 when DMA enabled, or to `SOC_SPI_MAXIMUM_BUFFER_SIZE` if DMA is disabled.
        .flags = 0,       ///< Abilities of bus to be checked by the driver. Or-ed value of ``SPICOMMON_BUSFLAG_*`` flags.
        .intr_flags = 0,       //< Interrupt flag for the bus to set the priority, and IRAM attribute, see
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = nullptr;
    static esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = &disp_drv,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .flags = {
            .dc_low_on_data = 0,   /*!< If this flag is enabled, DC line = 0 means transfer data, DC line = 1 means transfer command; vice versa */
            .octal_mode = 0,       /*!< transmit with octal mode (8 data lines), this mode is used to simulate Intel 8080 timing */
            .sio_mode = 0,         /*!< Read and write through a single data line (MOSI) */
            .lsb_first = 0,        /*!< transmit LSB bit first */
            .cs_high_active = 0,   /*!< CS line is high active */
        },
    };

    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = nullptr;
    static esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = 0
        },
        .vendor_config = nullptr,
    };

    ESP_LOGI(TAG, "Install GC9A01 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);

    // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));


    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(LCD_PIN_BK, LCD_BK_LIGHT_ON_LEVEL);


    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = static_cast<lv_color_t*>(heap_caps_malloc(LCD_H_RES * LVGL_DRAW_BUFFER_ROWS * sizeof(lv_color_t), MALLOC_CAP_DMA));
    assert(buf1);
    lv_color_t *buf2 = static_cast<lv_color_t*>(heap_caps_malloc(LCD_H_RES * LVGL_DRAW_BUFFER_ROWS * sizeof(lv_color_t), MALLOC_CAP_DMA));
    assert(buf2);

    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LVGL_DRAW_BUFFER_ROWS);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = on_lvgl_flush;
    disp_drv.drv_update_cb = on_lvgl_drv_update;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    g_display = lv_disp_drv_register(&disp_drv);
    lv_disp_set_default(g_display);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    static esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = on_lvgl_tick,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_ISR,
        .name = "lvgl_tick",
        .skip_unhandled_events = false,
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));


    static StaticSemaphore_t sem_buffer;
    g_display_sem = xSemaphoreCreateBinaryStatic(&sem_buffer);
    xSemaphoreGive(g_display_sem);


    static StaticTask_t task_buffer;
    static StackType_t task_stack[LVGL_TASK_STACK_DEPTH];
    g_display_task = xTaskCreateStatic(display_task, "lvgl_render", LVGL_TASK_STACK_DEPTH, &disp_drv, LVGL_TASK_PRIORITY, task_stack, &task_buffer);

    return g_display;
}


bool display_acquire(TickType_t ticksToWait)
{
    return xSemaphoreTake(g_display_sem, ticksToWait)==pdTRUE;
}

void display_release()
{
    xSemaphoreGive(g_display_sem);
}


void display_set_backlight(bool enable)
{
    gpio_set_level(LCD_PIN_BK, enable?LCD_BK_LIGHT_ON_LEVEL:LCD_BK_LIGHT_OFF_LEVEL);
    g_backlight = enable;
}

bool display_get_backlight()
{
    return g_backlight;
}
