#include "console.h"

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_chip_info.h>
#include <esp_idf_version.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_flash.h>
#include <esp_console.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <argtable3/argtable3.h>

#include "app_base.h"
#include "wifi.h"


#define PROMPT_STR CONFIG_IDF_TARGET

static constexpr uint CONSOLE_MAX_COMMAND_LINE_LENGTH { 256 };
//static constexpr uint CONSOLE_DELAY { 10 };
static constexpr const char* TAG = "console";

static esp_console_repl_t *g_repl = NULL;


/* 'version' command */
static int cmd_info(int argc, char **argv)
{
    const char *model;
    esp_chip_info_t info;
    esp_chip_info(&info);

    switch(info.model) {
        case CHIP_ESP32:
            model = "ESP32";
            break;
        case CHIP_ESP32S2:
            model = "ESP32-S2";
            break;
        case CHIP_ESP32S3:
            model = "ESP32-S3";
            break;
        case CHIP_ESP32C3:
            model = "ESP32-C3";
            break;
        case CHIP_ESP32H2:
            model = "ESP32-H2";
            break;
        case CHIP_ESP32C2:
            model = "ESP32-C2";
            break;
        default:
            model = "Unknown";
            break;
    }

    uint32_t flash_size;
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return 1;
    }
    uint32_t heap_size = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);

    printf("Chip info:\n");
    printf("     model: %s v%u.%u\n", model, info.revision/100, info.revision%100);
    printf("     cores: %d\n", info.cores);
    printf("      heap: %lu%s\n", heap_size," KB");
    printf("     flash: %lu%s%s\n", flash_size / (1024 * 1024), " MB", info.features & CHIP_FEATURE_EMB_FLASH ? " Embedded-Flash" : " External-Flash");
    printf("   feature: %s%s%s%s\n",
           info.features & CHIP_FEATURE_WIFI_BGN ? "/802.11bgn" : "",
           info.features & CHIP_FEATURE_BLE ? "/BLE" : "",
           info.features & CHIP_FEATURE_BT ? "/BT" : "",
           info.features & CHIP_FEATURE_EMB_PSRAM ? "/PSRAM" : "");

    const esp_app_desc_t *app_desc = esp_app_get_description();

    printf("\nApplication info:\n");
    printf("   project: %s\n", app_desc->project_name);
    printf("   version: %s\n", app_desc->version);
    printf("  compiled: %s, %s\n", app_desc->date, app_desc->time);
    printf("   idf ver: %s\n", app_desc->idf_ver);

    return 0;
}

static int cmd_restart(int argc, char **argv)
{
    ESP_LOGI(TAG, "Restarting");
    esp_restart();
}


static int cmd_mem(int argc, char **argv)
{
    uint32_t sz = esp_get_free_heap_size();
    uint32_t min_sz = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    uint32_t total_sz = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    printf("Heap info:\n");
    printf("      Total: %lu (%lu KB)\n", total_sz, total_sz/1024);
    printf("       Free: %lu (%lu KB)\n", sz, sz/1024);
    printf("  Low water: %lu (%lu KB)\n", min_sz, min_sz/1024);
    return 0;
}

static int cmd_heap(int argc, char **argv) 
{
    heap_caps_dump_all();
    return 0;
}




static int cmd_tasks(int argc, char **argv) 
{
    TaskStatus_t *status = nullptr;
    UBaseType_t status_size;
    UBaseType_t status_count;
    uint32_t total_runtime = 0;

    status_size = uxTaskGetNumberOfTasks() + 5;
    status = new TaskStatus_t[status_size];

    status_count = uxTaskGetSystemState(status, status_size, &total_runtime);

    qsort(status, status_count, sizeof(TaskStatus_t), [](auto a_, auto b_) { 
        auto a = static_cast<const TaskStatus_t*>(a_);
        auto b = static_cast<const TaskStatus_t*>(b_);
        if (a->uxCurrentPriority==b->uxCurrentPriority) {
            return strcmp(a->pcTaskName, b->pcTaskName);
        }
        return a->uxCurrentPriority>b->uxCurrentPriority ? -1 : 1;
    });


    printf("Name           State  Pri  Core   Stack               CPU\n");
    printf("---------------------------------------------------------\n");

    total_runtime /= 100;
    for (uint i=0; i<status_count; i++) {
        auto &st = status[i];
        char state = ' ';
        uint32_t percentage = st.ulRunTimeCounter / total_runtime;

        switch (st.eCurrentState) {
            case eRunning:     /* A task is querying the state of itself, so must be running. */
                state = 'X';
                break;
            case eReady:       /* The task being queried is in a ready or pending ready list. */
                state = 'R';
                break;
            case eBlocked:     /* The task being queried is in the Blocked state. */
                state = 'B';
                break;
            case eSuspended:   /* The task being queried is in the Suspended state, or is in the Blocked state with an infinite time out. */
                state = 'S';
                break;
            case eDeleted:     /* The task being queried has been deleted, but its TCB has not yet been freed. */
                state = 'D';
                break;
            case eInvalid:     /* Used as an 'invalid state' value. */
                state = '!';
                break;
        }

        printf("%-16s %c     %2u     %c %7lu %12lu %3lu%%\n",
            st.pcTaskName, 
            state,
            st.uxCurrentPriority,
            st.xCoreID==tskNO_AFFINITY ? '-' : ('0'+st.xCoreID),
            st.usStackHighWaterMark,
            st.ulRunTimeCounter,
            percentage
            );
    }

    delete[] status;
    return 0;
}



/** -------------------------------------------------------------------------------
 * Time date commands
 */



static int cmd_date(int argc, char **argv) 
{
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];
    
    time(&now);
    localtime_r(&now, &timeinfo);

    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);

    printf("Current date: %s\n", strftime_buf);

    return 0;
}



static struct {
    struct arg_str *tz;
    struct arg_end *end;
} set_timezone_args;

static int cmd_set_timezone(int argc, char **argv) 
{
    int nerrors = arg_parse(argc, argv, (void **) &set_timezone_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_timezone_args.end, argv[0]);
        return 1;
    }
    const char *timezone = set_timezone_args.tz->sval[0];

    ESP_LOGI(TAG, "Set timezone '%s'", timezone);

    if (!app_set_timezone(timezone)) {
        ESP_LOGI(TAG, "Set timezone failed");
        return 1;
    }

    return 0;
}


static struct {
    struct arg_str *server;
    struct arg_end *end;
} set_ntp_server_args;

static int cmd_set_ntp_server(int argc, char **argv) 
{
    int nerrors = arg_parse(argc, argv, (void **) &set_ntp_server_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_ntp_server_args.end, argv[0]);
        return 1;
    }
    const char *server = set_ntp_server_args.server->sval[0];

    ESP_LOGI(TAG, "Set NTP server '%s'", server);

    if (!app_set_ntp_server(server)) {
        ESP_LOGI(TAG, "Set NTP server failed");
        return 1;
    }

    return 0;
}



/** -------------------------------------------------------------------------------
 * Wifi commands
 */

static constexpr uint JOIN_TIMEOUT_MS { 10000 };


static struct {
    struct arg_int *timeout;
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} join_args;

static int cmd_wifi_join(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &join_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, join_args.end, argv[0]);
        return 1;
    }
    ESP_LOGI(TAG, "Connecting to '%s'", join_args.ssid->sval[0]);

    /* set default value*/
    if (join_args.timeout->count == 0) {
        join_args.timeout->ival[0] = JOIN_TIMEOUT_MS;
    }

    bool connected = wifi_join(join_args.ssid->sval[0],
                            join_args.password->sval[0],
                            join_args.timeout->ival[0]);
    if (!connected) {
        ESP_LOGW(TAG, "Connection timed out");
        return 1;
    }
    ESP_LOGI(TAG, "Connected");
    return 0;
}


static struct {
    struct arg_str *code;
    struct arg_end *end;
} cc_args;

static int cmd_wifi_set_country(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &cc_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cc_args.end, argv[0]);
        return 1;
    }


    ESP_LOGI(TAG, "Set country code to '%s'", cc_args.code->sval[0]);

    auto res = esp_wifi_set_country_code(cc_args.code->sval[0], true);
    if (res!=ESP_OK) {
        ESP_LOGW(TAG, "Set country code failed with code %d", res);
        return 1;
    }


    return 0;
}


static int cmd_wifi_restore(int argc, char **argv) {
    if (!wifi_restore()) {
        ESP_LOGW(TAG, "WiFi restore command failed");
        return 1;
    }
    ESP_LOGI(TAG, "WiFi settings reset to default");
    return 0;
}




/** -------------------------------------------------------------------------------
 * Global
 */


static void register_commands()
{
    {
        static constexpr esp_console_cmd_t cmd = {
            .command = "info",
            .help = "Get version of chip and SDK",
            .hint = NULL,
            .func = &cmd_info,
            .argtable = nullptr,
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }

    {
        static constexpr esp_console_cmd_t cmd = {
            .command = "restart",
            .help = "Software reset of the chip",
            .hint = NULL,
            .func = &cmd_restart,
            .argtable = nullptr,
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }

    {
        const esp_console_cmd_t cmd = {
            .command = "mem",
            .help = "Get the current status of heap memory",
            .hint = NULL,
            .func = &cmd_mem,
            .argtable = nullptr,
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }

    {
        const esp_console_cmd_t cmd = {
            .command = "heap",
            .help = "Print heap info",
            .hint = NULL,
            .func = &cmd_heap,
            .argtable = nullptr,
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }

    {
        const esp_console_cmd_t cmd = {
            .command = "tasks",
            .help = "Print task info",
            .hint = NULL,
            .func = &cmd_tasks,
            .argtable = nullptr,
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }

    {
        const esp_console_cmd_t cmd = {
            .command = "date",
            .help = "Current date",
            .hint = NULL,
            .func = &cmd_date,
            .argtable = nullptr,
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }

    {
        set_timezone_args.tz = arg_str1(nullptr, nullptr, "<tz>", "Timezone");
        set_timezone_args.end = arg_end(2);

        const esp_console_cmd_t cmd = {
            .command = "set_timezone",
            .help = "Set system timezone",
            .hint = nullptr,
            .func = &cmd_set_timezone,
            .argtable = &set_timezone_args
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }

    {
        set_ntp_server_args.server = arg_str1(nullptr, nullptr, "<server>", "NTP server");
        set_ntp_server_args.end = arg_end(2);

        const esp_console_cmd_t cmd = {
            .command = "set_ntp_server",
            .help = "Set NTP server",
            .hint = nullptr,
            .func = &cmd_set_ntp_server,
            .argtable = &set_ntp_server_args
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }


    // Wifi ----------------------------------------------------
    {
        join_args.timeout = arg_int0(nullptr, "timeout", "<t>", "Connection timeout, ms");
        join_args.ssid = arg_str1(nullptr, nullptr, "<ssid>", "SSID of AP");
        join_args.password = arg_str0(nullptr, nullptr, "<pass>", "PSK of AP");
        join_args.end = arg_end(2);

        const esp_console_cmd_t cmd = {
            .command = "wifi_join",
            .help = "Join WiFi AP as a station",
            .hint = nullptr,
            .func = &cmd_wifi_join,
            .argtable = &join_args
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }

    {
        cc_args.code = arg_str1(nullptr, nullptr, "<code>", "Wifi Country code");
        cc_args.end = arg_end(2);

        const esp_console_cmd_t cmd = {
            .command = "wifi_set_country",
            .help = "Set wifi country code",
            .hint = nullptr,
            .func = &cmd_wifi_set_country,
            .argtable = &cc_args
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }

    {
        const esp_console_cmd_t cmd = {
            .command = "wifi_restore",
            .help = "Restore wifi settings to default",
            .hint = nullptr,
            .func = &cmd_wifi_restore,
            .argtable = nullptr,
        };
        ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
    }

}




void console_init()
{
    esp_console_register_help_command();
    register_commands();

    static esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = CONSOLE_MAX_COMMAND_LINE_LENGTH;

    #if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
        esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
        ESP_LOGI(TAG, "UART %p", repl);

    #elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
        esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &g_repl));
        ESP_LOGI(TAG, "JTAG %p", g_repl);

    #else
        #error Unsupported console type
    #endif

    ESP_ERROR_CHECK(esp_console_start_repl(g_repl));
}