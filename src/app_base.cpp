#include "app_base.h"

#include <time.h>
#include <esp_spiffs.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <esp_log.h>

#include "wifi.h"

static constexpr char TAG[] = "app";

static constexpr char ENVIRONMENT_NVS_NAMESPACE[] { "env" };



static esp_event_loop_handle_t g_loop;



void app_base_init()
{
    esp_err_t ret;

    ESP_LOGI(TAG, "NVS init");
    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    

    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
 
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        esp_system_abort("Error initializing SPIFFS");
    }

    #if 0
    ESP_LOGI(TAG, "Performing SPIFFS_check().");
    ret = esp_spiffs_check(conf.partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
        return;
    } else {
        ESP_LOGI(TAG, "SPIFFS_check() successful");
    }
    #endif

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
        esp_spiffs_format(conf.partition_label);
        return;
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }


    // Check consistency of reported partiton size info.
    if (used > total) {
        ESP_LOGW(TAG, "Number of used bytes cannot be larger than total. Performing SPIFFS_check().");
        ret = esp_spiffs_check(conf.partition_label);
        // Could be also used to mend broken files, to clean unreferenced pages, etc.
        // More info at https://github.com/pellepl/spiffs/wiki/FAQ#powerlosses-contd-when-should-i-run-spiffs_check
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
            return;
        } else {
            ESP_LOGI(TAG, "SPIFFS_check() successful");
        }
    }


    // Init timezone
    ESP_LOGI(TAG, "Initializing system timezone");
    nvs_handle_t handle;
    ret = nvs_open(ENVIRONMENT_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret==ESP_OK) {
        size_t len = 0;

        if (nvs_get_str(handle, "TZ", nullptr, &len)==ESP_OK) {
            char tz[len];
            nvs_get_str(handle, "TZ", tz, &len);
            ESP_LOGI(TAG, "Setting system timezone: %s", tz);
            setenv("TZ", tz, 1);
            tzset();
        }

        if (nvs_get_str(handle, "NTP_SERVER", nullptr, &len)==ESP_OK) {
            char ntp[len];
            nvs_get_str(handle, "NTP_SERVER", ntp, &len);
            ESP_LOGI(TAG, "Setting system NTP server: %s", ntp);
            wifi_sntp_set_server(ntp);
        }
        else {
            ESP_LOGI(TAG, "Using default NTP server");
            wifi_sntp_set_server("pool.ntp.org");
        }

        nvs_close(handle);
    }




    static constexpr esp_event_loop_args_t loop_args = {
        .queue_size = 10,
        .task_name = nullptr, // no task will be created
        .task_priority = 0,
        .task_stack_size = 0,
        .task_core_id = tskNO_AFFINITY,
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &g_loop));


#if 0
    esp_event_loop_handle_t hndl;

    esp
#endif

}

static bool save_env(const char *env, const char *value)
{
    nvs_handle_t handle;
    esp_err_t res;

    res = nvs_open(ENVIRONMENT_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (res!=ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS store: err=%d", res);
        return false;
    }

    res = nvs_set_str(handle, env, value);
    if (res!=ESP_OK) {
        ESP_LOGE(TAG, "Error storing NVS env %s: err=%d", env, res);
        nvs_close(handle);
        return false;
    }

    res = nvs_commit(handle);
    if (res!=ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: err=%d", res);
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    return true;
}


bool app_set_timezone(const char *tz)
{
    if (!save_env("TS", tz)) {
        return false;
    }
    setenv("TZ", tz, 1);
    tzset();

    return true;
}



bool app_set_ntp_server(const char *ntp_server)
{
    if (!save_env("NTP_SERVER", ntp_server)) {
        return false;
    }
    wifi_sntp_set_server(ntp_server);
    return true;
}


esp_err_t app_event_loop_run(TickType_t ticks_to_run)
{
    return esp_event_loop_run(g_loop, ticks_to_run);
}

esp_err_t app_event_handler_register(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg)
{
    return esp_event_handler_register_with(g_loop, event_base, event_id, event_handler, event_handler_arg);
}

esp_err_t app_event_post(esp_event_base_t event_base, int32_t event_id, const void *event_data, size_t event_data_size, TickType_t ticks_to_wait)
{
    return esp_event_post_to(g_loop, event_base, event_id, event_data, event_data_size, ticks_to_wait);
}

