/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi_server.h"
#include "esp_log.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "file_manager.h"
#include "photo_album.h"

static const char *TAG = "wifi_server";

// Global state
static struct {
    wifi_server_config_t config;
    wifi_server_status_t status;
    char ip_address[16];
    uint8_t client_count;
    bool initialized;
} s_wifi_server = {
    .status = WIFI_SERVER_STOPPED,
    .client_count = 0,
    .initialized = false
};

// HTTP server instance
static httpd_handle_t s_httpd = NULL;

// Utility: start SoftAP with given config
static esp_err_t wifi_start_softap(const wifi_server_config_t *cfg)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Starting SoftAP: %s", cfg->ap_ssid);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    wifi_config_t ap_cfg = { 0 };
    strncpy((char *)ap_cfg.ap.ssid, cfg->ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(cfg->ap_ssid);
    strncpy((char *)ap_cfg.ap.password, cfg->ap_password, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.channel = 6;
    ap_cfg.ap.max_connection = cfg->max_clients ? cfg->max_clients : 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (strlen(cfg->ap_password) == 0) {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ret = esp_wifi_start();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi AP started. SSID:%s", cfg->ap_ssid);
    }
    return ret;
}

/* --------------- HTTP Handlers --------------- */

#define JSON_BUF_SIZE (4096)

static esp_err_t api_list_handler(httpd_req_t *req)
{
    photo_collection_t collection = {0};
    collection.files = calloc(MAX_FILES_COUNT, sizeof(image_file_info_t));
    if (!collection.files) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No mem");
    }

    collection.scan_subdirs = false;
    esp_err_t ret = file_manager_scan_images(PHOTO_BASE_PATH, &collection);
    if (ret != ESP_OK) {
        free(collection.files);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan fail");
    }

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < collection.total_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", collection.files[i].filename);
        cJSON_AddNumberToObject(item, "size", (double)collection.files[i].file_size);
        cJSON_AddItemToArray(root, item);
    }

    char *resp_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(collection.files);

    if (!resp_str) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON fail");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    return ESP_OK;
}

static esp_err_t photo_download_handler(httpd_req_t *req)
{
    const char *uri = req->uri; // e.g., /photos/filename.jpg
    const char *fname = uri + strlen("/photos/");
    if (strlen(fname) == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No file");
    }

    char path[MAX_FILENAME_LEN];
    snprintf(path, sizeof(path), "%s/%s", PHOTO_BASE_PATH, fname);

    FILE *f = fopen(path, "rb");
    if (!f) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    }

    httpd_resp_set_type(req, "application/octet-stream");
    char chunk[8192];
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0); // End response
    return ESP_OK;
}

static esp_err_t start_http_server(void)
{
    if (s_httpd) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t ret = httpd_start(&s_httpd, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start httpd: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register /api/list
    httpd_uri_t list_uri = {
        .uri = "/api/list",
        .method = HTTP_GET,
        .handler = api_list_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_httpd, &list_uri);

    // Register /photos/* download
    httpd_uri_t photo_uri = {
        .uri = "/photos/*",
        .method = HTTP_GET,
        .handler = photo_download_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_httpd, &photo_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

/* ----------- Public API implementations ------------ */

esp_err_t wifi_server_init(const wifi_server_config_t *config)
{
    if (s_wifi_server.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    // Copy configuration
    s_wifi_server.config = *config;
    strcpy(s_wifi_server.ip_address, "192.168.4.1");

    if (config->enable_wifi) {
        ESP_LOGI(TAG, "WiFi server configured - SSID: %s", config->ap_ssid);
        s_wifi_server.status = WIFI_SERVER_STOPPED;
    }

    s_wifi_server.initialized = true;
    return ESP_OK;
}

esp_err_t wifi_server_start(const char *base_path)
{
    if (s_wifi_server.status == WIFI_SERVER_RUNNING) {
        return ESP_OK;
    }

    esp_err_t ret = wifi_start_softap(&s_wifi_server.config);
    if (ret != ESP_OK) {
        s_wifi_server.status = WIFI_SERVER_ERROR;
        return ret;
    }

    ret = start_http_server();
    if (ret != ESP_OK) {
        s_wifi_server.status = WIFI_SERVER_ERROR;
        return ret;
    }

    s_wifi_server.status = WIFI_SERVER_RUNNING;
    return ESP_OK;
}

esp_err_t wifi_server_stop(void)
{
    if (s_wifi_server.status == WIFI_SERVER_STOPPED) {
        return ESP_OK;
    }

    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    esp_wifi_stop();
    s_wifi_server.status = WIFI_SERVER_STOPPED;
    return ESP_OK;
}

esp_err_t wifi_server_deinit(void)
{
    if (!s_wifi_server.initialized) {
        return ESP_OK;
    }

    wifi_server_stop();
    s_wifi_server.initialized = false;
    
    ESP_LOGI(TAG, "WiFi server deinitialized");
    return ESP_OK;
}

wifi_server_status_t wifi_server_get_status(void)
{
    return s_wifi_server.status;
}

bool wifi_server_is_running(void)
{
    return (s_wifi_server.status == WIFI_SERVER_RUNNING);
}

uint8_t wifi_server_get_client_count(void)
{
    return s_wifi_server.client_count;
}

const char* wifi_server_get_ip_address(void)
{
    return s_wifi_server.ip_address;
} 