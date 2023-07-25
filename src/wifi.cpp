#include "wifi.h"

#include "wifi.h"

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <esp_event.h>
#include <esp_system.h>
#include <esp_netif.h>
#include <esp_rrm.h>
#include <esp_wnm.h>
#include <esp_sntp.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>
#include <nvs.h>


static constexpr const char* TAG = "wifi";

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
static constexpr int WIFI_ENABLE_BIT      = BIT0;
static constexpr int WIFI_CONNECTED_BIT   = BIT1;
static constexpr int WIFI_IP_BIT          = BIT2;

static char g_sntp_server[64] = "";


static void time_sync_notification_cb(struct timeval *tv)
{
    char strftime_buf[64];
    struct tm timeinfo;
   
    localtime_r(&tv->tv_sec, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);

    ESP_LOGI(TAG, "Time synchronized %s", strftime_buf);
}


static inline void wifi_event_handler(int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            {
                ESP_LOGI(TAG, "WIFI started");
                if (xEventGroupGetBits(s_wifi_event_group) & WIFI_ENABLE_BIT) {
                    auto res = esp_wifi_connect();
                    if (res==ESP_ERR_WIFI_SSID) {
                        ESP_LOGW(TAG, "SSID is invalid");
                    }
                }
            }
            break;

        case WIFI_EVENT_STA_CONNECTED:
            {
                wifi_event_sta_connected_t *event = static_cast<wifi_event_sta_connected_t*>(event_data);
                ESP_LOGI(TAG, "WIFI connected:   ssid=%s,  channel=%d,  authmode=%d", event->ssid, (int)event->channel, (int)event->authmode);
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

                if (esp_rrm_is_rrm_supported_connection()) {
                    ESP_LOGI(TAG,"RRM supported");
                } else {
                    ESP_LOGI(TAG,"RRM not supported");
                }
                if (esp_wnm_is_btm_supported_connection()) {
                    ESP_LOGI(TAG,"BTM supported");
                } else {
                    ESP_LOGI(TAG,"BTM not supported");
                }
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            {
                wifi_event_sta_disconnected_t *event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
                if (event->reason == WIFI_REASON_ROAMING) {
                    ESP_LOGI(TAG, "station roaming");
                }
                else {
                    ESP_LOGW(TAG, "WIFI disconnected    reason=%d", (int)event->reason);
                    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                    if (xEventGroupGetBits(s_wifi_event_group) & WIFI_ENABLE_BIT) {
                        esp_wifi_connect();
                    }
                }
            }
            break;
                
        default:
            ESP_LOGW(TAG, "WIFI Unknown Event %ld", event_id);
            break;
    }
}


static inline void ip_event_handler(int32_t event_id, void *event_data)
{
    switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
                xEventGroupSetBits(s_wifi_event_group, WIFI_IP_BIT);
                if (sntp_get_sync_status()!=SNTP_SYNC_STATUS_COMPLETED) {
                    esp_sntp_restart();
                }
            }
            break;

        case IP_EVENT_STA_LOST_IP:
            ESP_LOGW(TAG, "Lost IP");
            xEventGroupClearBits(s_wifi_event_group, WIFI_IP_BIT);
            break;

        default:
            ESP_LOGW(TAG, "IP Unknown Event %ld", event_id);
            break;
    }
}



static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base==WIFI_EVENT) {
        wifi_event_handler(event_id, event_data);
    }
    else if (event_base == IP_EVENT) {
        ip_event_handler(event_id, event_data);
    }
    else {
        ESP_LOGW(TAG, "Unknown Event %p %ld", event_base, event_id);
    }
}


static void wifi_sntp_init()
{
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, g_sntp_server);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

void wifi_sntp_set_server(const char *servername)
{
    strncpy(g_sntp_server, servername, sizeof(g_sntp_server));
    g_sntp_server[sizeof(g_sntp_server)-1] = '\0';
}



void wifi_init()
{
    ESP_LOGI(TAG, "Initializing wifi");

    ESP_ERROR_CHECK( esp_netif_init() );
    static StaticEventGroup_t event_group_buffer;
    s_wifi_event_group = xEventGroupCreateStatic(&event_group_buffer);
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &event_handler, NULL) );

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );

    wifi_config_t wifi_config;
    ESP_ERROR_CHECK( esp_wifi_get_config(WIFI_IF_STA, &wifi_config) );
    ESP_LOGI(TAG, "Configured SSID: %s", wifi_config.sta.ssid);
    //ESP_LOGI(TAG, "PASSWORD: %s", wifi_config.sta.password);

    char cc[3] { 0 };
    ESP_ERROR_CHECK( esp_wifi_get_country_code(cc) );
    cc[2] = '\0';
    ESP_LOGI(TAG, "Configured Country: %s", cc);


    ESP_LOGI(TAG, "Initializing SNTP service");
    wifi_sntp_init();


    ESP_LOGI(TAG, "Starting WiFi");

    xEventGroupSetBits(s_wifi_event_group, WIFI_ENABLE_BIT);
    ESP_ERROR_CHECK( esp_wifi_start() );
}


bool wifi_join(const char *ssid, const char *password, uint timeout_ms)
{
    wifi_config_t wifi_config;
    memset(&wifi_config, 0x00, sizeof(wifi_config));
    esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

    wifi_sta_config_t &sta = wifi_config.sta;
    sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta.pmf_cfg.capable = true;
    sta.pmf_cfg.required = false;
    sta.rm_enabled = 1;
	sta.btm_enabled =1;
	sta.mbo_enabled =1;
	sta.ft_enabled =1;


    sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    strlcpy((char *)sta.ssid, ssid, sizeof(sta.ssid));
    if (password) {
        strlcpy((char *) sta.password, password, sizeof(sta.password));
    }

    xEventGroupClearBits(s_wifi_event_group, WIFI_ENABLE_BIT);
    ESP_ERROR_CHECK( esp_wifi_disconnect() );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    xEventGroupSetBits(s_wifi_event_group, WIFI_ENABLE_BIT);
    ESP_ERROR_CHECK( esp_wifi_connect() );

    auto bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_IP_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}


bool wifi_restore()
{
    xEventGroupClearBits(s_wifi_event_group, WIFI_ENABLE_BIT);
    ESP_ERROR_CHECK( esp_wifi_disconnect() );
    auto res = esp_wifi_restore();
    if (res!=ESP_OK) {
        ESP_LOGW(TAG, "Error restoring configuration  res=%d", (int)res);
        return false;
    }
    ESP_LOGI(TAG, "Wifi configuration restored to default");
    return true;
}
