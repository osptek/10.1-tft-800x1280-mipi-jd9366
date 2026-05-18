/* SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/ip_addr.h"
#include "app_wifi.h"
#include "esp_mac.h"
#include "sdkconfig.h"

/* Default WiFi configuration */
#define DEFAULT_AP_SSID       CONFIG_WIFI_AP_SSID
#define DEFAULT_AP_PASSWORD   CONFIG_WIFI_AP_PASSWORD
#define DEFAULT_AP_CHANNEL    1
#define DEFAULT_MAX_STA_CONN  CONFIG_WIFI_AP_MAX_CLIENTS
#define DEFAULT_IP_ADDR       "192.168.4.1"

#define WIFI_SSID_LEN         32
#define WIFI_PASSWORD_LEN     64

static const char *TAG = "wifi";
static esp_netif_t *s_wifi_ap_netif = NULL;
static esp_netif_t *s_wifi_sta_netif = NULL;

// WiFi reconnection state
static struct {
    int retry_count;
    bool auto_retry_enabled;
    esp_timer_handle_t retry_timer;
} s_wifi_retry = {
    .retry_count = 0,
    .auto_retry_enabled = true,
    .retry_timer = NULL
};

#define MAX_RETRY_ATTEMPTS 5
#define RETRY_DELAY_MS 5000

static void wifi_retry_timer_callback(void *arg)
{
    if (s_wifi_retry.auto_retry_enabled && s_wifi_retry.retry_count < MAX_RETRY_ATTEMPTS) {
        ESP_LOGI(TAG, "Retrying WiFi connection (attempt %d/%d)", 
                 s_wifi_retry.retry_count + 1, MAX_RETRY_ATTEMPTS);
        esp_wifi_connect();
        s_wifi_retry.retry_count++;
    } else if (s_wifi_retry.retry_count >= MAX_RETRY_ATTEMPTS) {
        ESP_LOGW(TAG, "WiFi connection failed after %d attempts, please check credentials", 
                 MAX_RETRY_ATTEMPTS);
        s_wifi_retry.auto_retry_enabled = false;
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *) event_data;
        ESP_LOGI(TAG, "WiFi STA connected to SSID: %s, channel: %d", 
                 event->ssid, event->channel);
        // Reset retry count on successful connection
        s_wifi_retry.retry_count = 0;
        s_wifi_retry.auto_retry_enabled = true;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *) event_data;
        
        // Provide more detailed error information
        const char *reason_str = "Unknown";
        switch (event->reason) {
            case WIFI_REASON_AUTH_EXPIRE:
            case WIFI_REASON_AUTH_LEAVE:
            case WIFI_REASON_ASSOC_LEAVE:
                reason_str = "Authentication failed - check password";
                break;
            case WIFI_REASON_NO_AP_FOUND:
                reason_str = "Network not found - check SSID";
                break;
            case WIFI_REASON_CONNECTION_FAIL:
                reason_str = "Connection failed";
                break;
            case WIFI_REASON_ASSOC_FAIL:
                reason_str = "Association failed";
                break;
            default:
                reason_str = "Network disconnected";
                break;
        }
        
        ESP_LOGW(TAG, "WiFi disconnected from SSID: %s, reason: %d (%s)", 
                 event->ssid, event->reason, reason_str);
        
        // Use timer-based retry with backoff
        if (s_wifi_retry.auto_retry_enabled && s_wifi_retry.retry_count < MAX_RETRY_ATTEMPTS) {
            if (!s_wifi_retry.retry_timer) {
                esp_timer_create_args_t timer_args = {
                    .callback = wifi_retry_timer_callback,
                    .arg = NULL,
                    .name = "wifi_retry"
                };
                esp_timer_create(&timer_args, &s_wifi_retry.retry_timer);
            }
            
            ESP_LOGI(TAG, "Scheduling reconnection in %d ms...", RETRY_DELAY_MS);
            esp_timer_start_once(s_wifi_retry.retry_timer, RETRY_DELAY_MS * 1000);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "WiFi STA connected successfully!");
        ESP_LOGI(TAG, "IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "========================================");
        
        // No need to post custom event - let network manager handle IP_EVENT directly
    }
}

static void wifi_init_softap(esp_netif_t *wifi_netif)
{
    // Set custom IP if needed
    if (strcmp(DEFAULT_IP_ADDR, "192.168.4.1")) {
        esp_netif_ip_info_t ip;
        memset(&ip, 0, sizeof(esp_netif_ip_info_t));
        ip.ip.addr = ipaddr_addr(DEFAULT_IP_ADDR);
        ip.gw.addr = ipaddr_addr(DEFAULT_IP_ADDR);
        ip.netmask.addr = ipaddr_addr("255.255.255.0");
        ESP_ERROR_CHECK(esp_netif_dhcps_stop(wifi_netif));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(wifi_netif, &ip));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(wifi_netif));
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = DEFAULT_AP_SSID,
            .ssid_len = strlen(DEFAULT_AP_SSID),
            .password = DEFAULT_AP_PASSWORD,
            .max_connection = DEFAULT_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .channel = DEFAULT_AP_CHANNEL,
        },
    };

    if (strlen(DEFAULT_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));

    ESP_LOGI(TAG, "WiFi AP initialized. SSID:%s password:%s channel:%d",
             DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD, DEFAULT_AP_CHANNEL);
}

esp_err_t app_wifi_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create AP and STA netif
    s_wifi_ap_netif = esp_netif_create_default_wifi_ap();
    s_wifi_sta_netif = esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // Set WiFi mode to APSTA (both AP and STA)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Configure AP
    wifi_init_softap(s_wifi_ap_netif);

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi APSTA started successfully");
    return ESP_OK;
}

esp_err_t app_wifi_connect_sta(const char *ssid, const char *password)
{
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config = {0};
    strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (password) {
        strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    esp_err_t ret = esp_wifi_connect();
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", ssid);
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t app_wifi_get_ip_info(esp_netif_ip_info_t *ip_info)
{
    if (!s_wifi_ap_netif) {
        return ESP_FAIL;
    }

    return esp_netif_get_ip_info(s_wifi_ap_netif, ip_info);
}
