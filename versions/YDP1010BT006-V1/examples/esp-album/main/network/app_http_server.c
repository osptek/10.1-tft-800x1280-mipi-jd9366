/* SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <sys/param.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_http_server.h"
#include "app_http_server.h"
#include <sys/stat.h>
#include "modern_upload_page.h"
#include "cJSON.h"

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 64)

/* Max size of an individual file. Make sure this
 * value is same as that set in upload_script.html */
#define MAX_FILE_SIZE   (100*1024*1024) // 100 MB for large videos/images
#define MAX_FILE_SIZE_STR "100MB"

/* Scratch buffer size */
#define SCRATCH_BUFSIZE  8192

/* Error handling macro */
#define CLEANUP_AND_RETURN(code, msg) do { \
    ESP_LOGE(TAG, msg); \
    goto cleanup; \
} while(0)

struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];

    /* Upload callback for photo album refresh */
    upload_complete_callback_t upload_callback;
};

static const char *TAG = "file_server";

/* Forward declarations */
static const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize);
static esp_err_t url_decode(const char *src, char *dest, size_t dest_size);

/* Handler to redirect incoming GET request for /index.html to modern UI */
static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/modern_upload.html");
    httpd_resp_send(req, NULL, 0);  // Response body can be empty
    return ESP_OK;
}

/* Handler to respond with an icon file embedded in flash.
 * Browsers expect to GET website icon at URI /favicon.ico.
 * This can be overridden by uploading file with same name */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    // For simplicity, just return 204 No Content instead of embedded favicon
    httpd_resp_set_status(req, HTTPD_204);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Handler to serve the modern upload HTML page */
static esp_err_t modern_upload_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)modern_upload_html_start, get_modern_upload_html_size());
    return ESP_OK;
}

/* Handler to serve the modern upload CSS file */
static esp_err_t modern_upload_css_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)modern_upload_css_start, get_modern_upload_css_size());
    return ESP_OK;
}

/* Handler to serve the modern upload JavaScript file */
static esp_err_t modern_upload_js_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_send(req, (const char *)modern_upload_js_start, get_modern_upload_js_size());
    return ESP_OK;
}

/* Handler to serve file list as JSON for modern UI */
static esp_err_t files_json_get_handler(httpd_req_t *req)
{
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    char dirpath[FILE_PATH_MAX];

    strcpy(dirpath, server_data->base_path);

    DIR *dir = opendir(dirpath);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", dirpath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open directory");
        return ESP_FAIL;
    }

    cJSON *json_response = cJSON_CreateObject();
    cJSON *json_array = cJSON_CreateArray();
    if (!json_response || !json_array) {
        closedir(dir);
        if (json_response) {
            cJSON_Delete(json_response);
        }
        if (json_array) {
            cJSON_Delete(json_array);
        }
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON objects");
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(json_response, "current_dir", "");

    cJSON_AddStringToObject(json_response, "parent_dir", "");

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char full_path[FILE_PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dirpath, entry->d_name);

        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0) {
            if (S_ISDIR(file_stat.st_mode)) {
                ESP_LOGI(TAG, "Skipping directory: %s", entry->d_name);
                continue;
            }

            cJSON *item_obj = cJSON_CreateObject();
            if (item_obj) {
                cJSON_AddStringToObject(item_obj, "name", entry->d_name);
                cJSON_AddNumberToObject(item_obj, "size", file_stat.st_size);

                char rel_item_path[FILE_PATH_MAX];
                snprintf(rel_item_path, sizeof(rel_item_path), "/%s", entry->d_name);
                cJSON_AddStringToObject(item_obj, "path", rel_item_path);

                const char *ext = strrchr(entry->d_name, '.');
                if (ext) {
                    ext++;
                    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
                        cJSON_AddStringToObject(item_obj, "type", "image/jpeg");
                    } else if (strcasecmp(ext, "png") == 0) {
                        cJSON_AddStringToObject(item_obj, "type", "image/png");
                    } else if (strcasecmp(ext, "gif") == 0) {
                        cJSON_AddStringToObject(item_obj, "type", "image/gif");
                    } else if (strcasecmp(ext, "bmp") == 0) {
                        cJSON_AddStringToObject(item_obj, "type", "image/bmp");
                    } else if (strcasecmp(ext, "mp4") == 0) {
                        cJSON_AddStringToObject(item_obj, "type", "video/mp4");
                    } else if (strcasecmp(ext, "avi") == 0) {
                        cJSON_AddStringToObject(item_obj, "type", "video/avi");
                    } else if (strcasecmp(ext, "mov") == 0) {
                        cJSON_AddStringToObject(item_obj, "type", "video/mov");
                    } else {
                        cJSON_AddStringToObject(item_obj, "type", "application/octet-stream");
                    }
                } else {
                    cJSON_AddStringToObject(item_obj, "type", "application/octet-stream");
                }

                cJSON_AddItemToArray(json_array, item_obj);
            }
        }
    }

    closedir(dir);

    cJSON_AddItemToObject(json_response, "items", json_array);

    char *json_string = cJSON_Print(json_response);
    if (json_string) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
    }

    cJSON_Delete(json_response);
    return ESP_OK;
}

/* Handler to delete a file using DELETE method for modern UI */
static esp_err_t file_delete_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    const char *file_uri = req->uri + strlen("/delete");
    if (file_uri[0] == '/') {
        file_uri++;
    }

    if (strlen(file_uri) == 0) {
        ESP_LOGE(TAG, "No filename provided in delete request");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No filename provided");
        return ESP_FAIL;
    }

    snprintf(filepath, sizeof(filepath), "%s/%s", server_data->base_path, file_uri);

    ESP_LOGI(TAG, "Attempting to delete file: %s", filepath);

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "File does not exist: %s", file_uri);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleting file: %s", file_uri);
    if (unlink(filepath) != 0) {
        ESP_LOGE(TAG, "Failed to delete file: %s", file_uri);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete file");
        return ESP_FAIL;
    }

    // Call photo album refresh callback if provided
    if (server_data && server_data->upload_callback) {
        server_data->upload_callback(NULL); // Refresh photo album after deletion
    }

    // Send JSON response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"success\": true}", 18);
    return ESP_OK;
}

/**
 * @brief Check if file extension is supported by digital photo album
 */
static bool is_supported_media_file(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    ext++; // Skip the dot

    // Image formats
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0 ||
            strcasecmp(ext, "png") == 0 || strcasecmp(ext, "gif") == 0 ||
            strcasecmp(ext, "bmp") == 0) {
        return true;
    }

    // Video formats
    if (strcasecmp(ext, "mp4") == 0 || strcasecmp(ext, "avi") == 0 ||
            strcasecmp(ext, "mov") == 0) {
        return true;
    }

    return false;
}

/**
 * @brief Formats a given size in bytes into a human-readable string.
 *
 * This function converts the size in bytes to a more readable format,
 * displaying it in B (bytes), KB (kilobytes), MB (megabytes), or GB (gigabytes),
 * depending on the size of the input.
 *
 * @param bytes The size in bytes to be formatted.
 *
 * @return A pointer to a statically allocated buffer containing the formatted size as a string.
 *         The returned string will be in the format of "XX B", "XX KB", "XX MB", or "XX GB".
 *
 * @note The returned buffer is statically allocated within the function. The content will be
 *       overwritten with each call to this function.
 */
static const char* format_size(unsigned long long bytes)
{
    static char buffer[50];

    if (bytes < 1024) {
        snprintf(buffer, sizeof(buffer), "%llu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buffer, sizeof(buffer), "%lld KB", (long long)round(bytes / 1024.0));
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buffer, sizeof(buffer), "%lld MB", (long long)round(bytes / (1024.0 * 1024)));
    } else {
        snprintf(buffer, sizeof(buffer), "%lld GB", (long long)round(bytes / (1024.0 * 1024 * 1024)));
    }

    return buffer;
}

/* Handler to serve modern UI for directory access */
static esp_err_t serve_modern_ui_for_directory(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)modern_upload_html_start, get_modern_upload_html_size());
    return ESP_OK;
}

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".jpeg") || IS_FILE_EXT(filename, ".jpg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".png")) {
        return httpd_resp_set_type(req, "image/png");
    } else if (IS_FILE_EXT(filename, ".gif")) {
        return httpd_resp_set_type(req, "image/gif");
    } else if (IS_FILE_EXT(filename, ".bmp")) {
        return httpd_resp_set_type(req, "image/bmp");
    } else if (IS_FILE_EXT(filename, ".mp4")) {
        return httpd_resp_set_type(req, "video/mp4");
    } else if (IS_FILE_EXT(filename, ".avi")) {
        return httpd_resp_set_type(req, "video/x-msvideo");
    } else if (IS_FILE_EXT(filename, ".mov")) {
        return httpd_resp_set_type(req, "video/quicktime");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

static esp_err_t url_decode(const char *src, char *dest, size_t dest_size)
{
    if (!src || !dest || dest_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t src_len = strlen(src);
    size_t dest_idx = 0;

    for (size_t i = 0; i < src_len && dest_idx < dest_size - 1; i++) {
        if (src[i] == '%' && i + 2 < src_len) {
            // URL percent encoding
            char hex_str[3] = {src[i + 1], src[i + 2], '\0'};
            char *endptr;
            long hex_val = strtol(hex_str, &endptr, 16);
            if (*endptr == '\0') {
                dest[dest_idx++] = (char)hex_val;
                i += 2;
            } else {
                dest[dest_idx++] = src[i];
            }
        } else if (src[i] == '+') {
            // URL encoding for spaces
            dest[dest_idx++] = ' ';
        } else {
            dest[dest_idx++] = src[i];
        }
    }

    dest[dest_idx] = '\0';
    return ESP_OK;
}

static esp_err_t create_directory_if_not_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        // Path exists, check if it's a directory
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Path exists but is not a directory: %s", path);
            return ESP_FAIL;
        }
    }

    // Directory doesn't exist, create it
    if (mkdir(path, 0755) != 0) {
        ESP_LOGE(TAG, "Failed to create directory: %s", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Created directory: %s", path);
    return ESP_OK;
}

/* Handler to download a file kept on the server */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', serve modern UI for directory access */
    if (filename[strlen(filename) - 1] == '/') {
        return serve_modern_ui_for_directory(req);
    }

    if (stat(filepath, &file_stat) == -1) {
        /* If file not present on SPIFFS check if URI
         * corresponds to one of the hardcoded paths */
        if (strcmp(filename, "/index.html") == 0) {
            return index_html_get_handler(req);
        } else if (strcmp(filename, "/favicon.ico") == 0) {
            return favicon_get_handler(req);
        }
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Parse Content-Type header and extract content type information
 *
 * @param req HTTP request object
 * @param content_type_buf Buffer to store content type (must be freed if heap allocated)
 * @param max_stack_size Maximum size for stack allocation
 * @param is_multipart Pointer to store multipart flag
 * @param is_heap Pointer to store heap allocation flag
 * @return ESP_OK on success, ESP_FAIL on failure
 */
static esp_err_t parse_content_type(httpd_req_t *req, char **content_type_buf,
                                    size_t max_stack_size, bool *is_multipart, bool *is_heap)
{
    *is_multipart = false;
    *content_type_buf = NULL;
    if (is_heap) {
        *is_heap = false;
    }

    size_t content_type_len = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (content_type_len == 0) {
        ESP_LOGW(TAG, "No Content-Type header found");
        return ESP_OK;
    }

    static char content_type_local[128];
    if (content_type_len < sizeof(content_type_local)) {
        *content_type_buf = content_type_local;
    } else {
        *content_type_buf = malloc(content_type_len + 1);
        if (!*content_type_buf) {
            ESP_LOGE(TAG, "Failed to allocate memory for Content-Type header");
            return ESP_FAIL;
        }
        if (is_heap) {
            *is_heap = true;
        }
    }

    if (httpd_req_get_hdr_value_str(req, "Content-Type", *content_type_buf, content_type_len + 1) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get Content-Type header value");
        if (*content_type_buf && is_heap && *is_heap) {
            free(*content_type_buf);
            *content_type_buf = NULL;
        }
        return ESP_FAIL;
    }

    if (strstr(*content_type_buf, "multipart/form-data")) {
        *is_multipart = true;
    }

    return ESP_OK;
}

/**
 * @brief Extract boundary string from multipart Content-Type header
 *
 * @param content_type Content-Type header value
 * @param boundary Buffer to store boundary (at least 128 bytes)
 * @return ESP_OK on success, ESP_FAIL if no boundary found
 */
static esp_err_t extract_multipart_boundary(const char *content_type, char *boundary, size_t boundary_size)
{
    if (!content_type || !boundary) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *boundary_start = strstr(content_type, "boundary=");
    if (!boundary_start) {
        ESP_LOGE(TAG, "No boundary found in Content-Type");
        return ESP_FAIL;
    }

    boundary_start += 9;

    // Handle quoted boundary
    if (*boundary_start == '"') {
        boundary_start++;
        const char *boundary_end = strchr(boundary_start, '"');
        if (boundary_end) {
            size_t len = boundary_end - boundary_start;
            if (len < boundary_size) {
                strncpy(boundary, boundary_start, len);
                boundary[len] = '\0';
                return ESP_OK;
            }
        }
    } else {
        // Unquoted boundary - copy until space, semicolon, or end
        size_t len = 0;
        while (boundary_start[len] && boundary_start[len] != ' ' &&
                boundary_start[len] != ';' && boundary_start[len] != '\r' &&
                boundary_start[len] != '\n' && len < boundary_size - 1) {
            boundary[len] = boundary_start[len];
            len++;
        }
        boundary[len] = '\0';
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to extract boundary");
    return ESP_FAIL;
}

/**
 * @brief Process multipart form data upload
 *
 * @param req HTTP request object
 * @param content_type Content-Type header value
 * @param filepath Output buffer for constructed file path
 * @param filepath_size Size of filepath buffer
 * @return ESP_OK on success, ESP_FAIL on failure
 */
static esp_err_t process_multipart_upload(httpd_req_t *req, const char *content_type,
                                          char *filepath, size_t filepath_size)
{
    char boundary[128] = {0};
    if (extract_multipart_boundary(content_type, boundary, sizeof(boundary)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid multipart boundary");
        return ESP_FAIL;
    }

    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    char *buf = server_data->scratch;
    int remaining = req->content_len;
    bool found_file = false;
    bool in_file_data = false;
    static char filename[256];
    FILE *fd = NULL;
    esp_err_t ret = ESP_FAIL;

    memset(filename, 0, sizeof(filename));

    // Process multipart data chunks
    while (remaining > 0) {
        int received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE - 1));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Failed to receive multipart data");
            break;
        }

        buf[received] = '\0';

        // Extract filename from Content-Disposition header
        if (!found_file) {
            char *content_disp = strstr(buf, "Content-Disposition:");
            if (content_disp) {
                char *filename_start = strstr(content_disp, "filename=\"");
                if (filename_start) {
                    filename_start += 10; // Skip 'filename="'
                    char *filename_end = strchr(filename_start, '"');
                    if (filename_end) {
                        size_t name_len = filename_end - filename_start;
                        if (name_len < sizeof(filename)) {
                            strncpy(filename, filename_start, name_len);
                            filename[name_len] = '\0';
                            ESP_LOGI(TAG, "Found filename: %s", filename);

                            if (!is_supported_media_file(filename)) {
                                ESP_LOGE(TAG, "Unsupported file format: %s", filename);
                                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unsupported file format");
                                goto cleanup;
                            }

                            // use base_path + filename, not support subdirectory
                            snprintf(filepath, filepath_size, "%s/%s", server_data->base_path, filename);

                            // ensure directory exists
                            if (create_directory_if_not_exists(server_data->base_path) != ESP_OK) {
                                ESP_LOGE(TAG, "Failed to create base directory");
                                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create directory");
                                goto cleanup;
                            }

                            fd = fopen(filepath, "w");
                            if (!fd) {
                                ESP_LOGE(TAG, "Failed to create file: %s", filepath);
                                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
                                goto cleanup;
                            }

                            found_file = true;
                        }
                    }
                }
            }

            // Look for start of file data (after double CRLF)
            char *data_start = strstr(buf, "\r\n\r\n");
            if (data_start && found_file) {
                data_start += 4;
                in_file_data = true;

                int data_len = received - (data_start - buf);
                if (data_len > 0) {
                    static char boundary_marker[256];
                    snprintf(boundary_marker, sizeof(boundary_marker), "\r\n--%s", boundary);
                    char *boundary_pos = strstr(data_start, boundary_marker);
                    if (boundary_pos) {
                        data_len = boundary_pos - data_start;
                        in_file_data = false;
                    }

                    if (data_len > 0 && fwrite(data_start, 1, data_len, fd) != data_len) {
                        ESP_LOGE(TAG, "File write failed");
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File write failed");
                        goto cleanup;
                    }

                    if (!in_file_data) {
                        ret = ESP_OK;
                        // continue to consume remaining bytes without writing
                    }
                }
            }
        } else if (in_file_data) {
            // Continue writing file data
            static char boundary_marker[256];
            snprintf(boundary_marker, sizeof(boundary_marker), "\r\n--%s", boundary);
            char *boundary_pos = strstr(buf, boundary_marker);

            int write_len = received;
            if (boundary_pos) {
                write_len = boundary_pos - buf;
                in_file_data = false;
            }

            if (write_len > 0 && fwrite(buf, 1, write_len, fd) != write_len) {
                ESP_LOGE(TAG, "File write failed");
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File write failed");
                goto cleanup;
            }

            if (!in_file_data) {
                ret = ESP_OK;
                // continue reading remaining form data
            }
        }

        remaining -= received;
    }

    if (!found_file) {
        ESP_LOGE(TAG, "No valid file found in multipart data");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No valid file in upload");
    }

    if (ret != ESP_OK && found_file) {
        ret = ESP_OK;
    }

cleanup:
    if (fd) {
        fclose(fd);
        if (ret != ESP_OK) {
            unlink(filepath); // Delete incomplete file
        }
    }
    return ret;
}

/**
 * @brief Process direct binary upload (application/octet-stream)
 *
 * @param req HTTP request object
 * @param filepath Output buffer for constructed file path
 * @param filepath_size Size of filepath buffer
 * @return ESP_OK on success, ESP_FAIL on failure
 */
static esp_err_t process_binary_upload(httpd_req_t *req, char *filepath, size_t filepath_size)
{
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;

    const char *filename = req->uri + strlen("/upload");
    if (filename[0] == '/') {
        filename++;
    }

    if (strlen(filename) == 0) {
        ESP_LOGE(TAG, "No filename provided in URI");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No filename provided");
        return ESP_FAIL;
    }

    snprintf(filepath, filepath_size, "%s/%s", server_data->base_path, filename);

    ESP_LOGI(TAG, "Binary upload target: %s", filepath);

    if (!is_supported_media_file(filename)) {
        ESP_LOGE(TAG, "Unsupported file format: %s", filename);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unsupported file format");
        return ESP_FAIL;
    }

    if (create_directory_if_not_exists(server_data->base_path) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create base directory");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create directory");
        return ESP_FAIL;
    }

    FILE *fd = fopen(filepath, "w");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to create file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
    int remaining = req->content_len;
    int received;

    while (remaining > 0) {
        ESP_LOGI(TAG, "Remaining size: %d", remaining);
        received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Failed to read file data");
            fclose(fd);
            unlink(filepath);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read file data");
            return ESP_FAIL;
        }

        if (fwrite(buf, 1, received, fd) != received) {
            ESP_LOGE(TAG, "Failed to write file data");
            fclose(fd);
            unlink(filepath);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file data");
            return ESP_FAIL;
        }

        remaining -= received;
    }

    fclose(fd);
    ESP_LOGI(TAG, "Binary file uploaded successfully: %s", filepath);
    return ESP_OK;
}

/* Handler to upload a file onto the server */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    char *content_type_buf = NULL;
    bool is_multipart = false;
    bool is_heap = false;
    esp_err_t ret = ESP_FAIL;

    // Parse Content-Type header
    if (parse_content_type(req, &content_type_buf, 128, &is_multipart, &is_heap) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse Content-Type");
        goto cleanup;
    }

    // Process upload based on content type
    if (is_multipart) {
        ESP_LOGI(TAG, "Processing multipart form upload");
        ret = process_multipart_upload(req, content_type_buf, filepath, sizeof(filepath));
    } else {
        ESP_LOGI(TAG, "Processing binary file upload");
        ret = process_binary_upload(req, filepath, sizeof(filepath));
    }

    if (ret != ESP_OK) {
        goto cleanup;
    }

    /* Skip image resolution validation for HTTP uploads; SD/USB scanning will still validate */

    /* Log file upload completion */
    ESP_LOGI(TAG, "File uploaded successfully: %s", filepath);

    /* Refresh photo album after upload */
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    if (server_data->upload_callback) {
        server_data->upload_callback(filepath);
    }

    /* Check if this is an AJAX request (modern UI) */
    char accept_header[128];
    if (httpd_req_get_hdr_value_str(req, "Accept", accept_header, sizeof(accept_header)) == ESP_OK) {
        if (strstr(accept_header, "application/json")) {
            /* Return JSON response for modern UI */
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_sendstr(req, "{\"success\": true, \"message\": \"File uploaded successfully\"}");
            ret = ESP_OK;
            goto cleanup;
        }
    }

    /* Redirect the user to the main index page for legacy UI */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    ret = ESP_OK;

cleanup:
    if (is_heap && content_type_buf) {
        free(content_type_buf);
    }
    return ret;
}

/* Handler to delete a file from the server */
static esp_err_t delete_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    /* Skip leading "/delete" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri  + sizeof("/delete") - 1, sizeof(filepath));
    if (!filename) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/') {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "File does not exist : %s", filename);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleting file : %s", filename);
    /* Delete file */
    unlink(filepath);

    /* Call photo album refresh callback if provided */
    struct file_server_data *server_data_local = (struct file_server_data *)req->user_ctx;
    if (server_data_local && server_data_local->upload_callback) {
        server_data_local->upload_callback(NULL); // Refresh photo album after deletion
    }

    /* Redirect onto root to see the updated file list */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File deleted successfully");
    return ESP_OK;
}

httpd_handle_t server = NULL;
static struct file_server_data *server_data = NULL;

/* Function to start the file server */
esp_err_t start_file_server(const char *base_path, upload_complete_callback_t callback)
{
    /* Validate file storage base path */
    if (!base_path) {
        ESP_LOGE(TAG, "base path can't be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if server is already running and stop it if necessary */
    if (server_data || server) {
        ESP_LOGW(TAG, "File server already running, stopping existing server first");
        stop_file_server();
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path, sizeof(server_data->base_path));
    server_data->upload_callback = callback;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 12;  // Increased to support modern UI routes
    config.max_req_hdr_len = 4096;  // Increase for multipart uploads

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start file server!");
        free(server_data);
        server_data = NULL;
        return ESP_FAIL;
    }

    /* URI handler for GET /upload - redirect to modern UI */
    httpd_uri_t upload_get = {
        .uri       = "/upload",
        .method    = HTTP_GET,
        .handler   = index_html_get_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &upload_get);

    /* URI handler for GET /upload/ - redirect to modern UI */
    httpd_uri_t upload_get_slash = {
        .uri       = "/upload/",
        .method    = HTTP_GET,
        .handler   = index_html_get_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &upload_get_slash);

    /* URI handler for root path - redirect to modern UI */
    httpd_uri_t root_get = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_html_get_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &root_get);

    /* URI handler for uploading files to server */
    httpd_uri_t file_upload = {
        .uri       = "/upload/*",   // Match all URIs of type /upload/path/to/file
        .method    = HTTP_POST,
        .handler   = upload_post_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_upload);

    /* URI handler for multipart form uploads (from HTML forms) */
    httpd_uri_t form_upload = {
        .uri       = "/upload",     // Exact match for form submissions
        .method    = HTTP_POST,
        .handler   = upload_post_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &form_upload);

    /* URI handler for deleting files from server */
    httpd_uri_t file_delete = {
        .uri       = "/delete/*",   // Match all URIs of type /delete/path/to/file
        .method    = HTTP_POST,
        .handler   = delete_post_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_delete);

    /* URI handler for modern upload HTML page */
    httpd_uri_t modern_upload_html = {
        .uri       = "/modern_upload.html",
        .method    = HTTP_GET,
        .handler   = modern_upload_html_get_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &modern_upload_html);

    /* URI handler for modern upload CSS file */
    httpd_uri_t modern_upload_css = {
        .uri       = "/modern_upload.css",
        .method    = HTTP_GET,
        .handler   = modern_upload_css_get_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &modern_upload_css);

    /* URI handler for modern upload JavaScript file */
    httpd_uri_t modern_upload_js = {
        .uri       = "/modern_upload.js",
        .method    = HTTP_GET,
        .handler   = modern_upload_js_get_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &modern_upload_js);

    /* URI handler for files JSON API */
    httpd_uri_t files_json = {
        .uri       = "/files",
        .method    = HTTP_GET,
        .handler   = files_json_get_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &files_json);

    /* URI handler for file deletion with DELETE method */
    httpd_uri_t file_delete_modern = {
        .uri       = "/delete/*",
        .method    = HTTP_DELETE,
        .handler   = file_delete_handler,
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &file_delete_modern);

    /* URI handler for getting uploaded files and directory listing */
    httpd_uri_t file_download = {
        .uri       = "/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = download_get_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    ESP_LOGI(TAG, "File server started successfully");
    ESP_LOGI(TAG, "Upload files at: http://192.168.4.1/");
    ESP_LOGI(TAG, "Modern UI available at: http://192.168.4.1/modern_upload.html");
    return ESP_OK;
}

/**
 * @brief Stop the file server and free allocated resources
 *
 * @return
 *     - ESP_OK: Server stopped successfully.
 *     - ESP_ERR_INVALID_STATE: Server is not running.
 */
esp_err_t stop_file_server(void)
{
    esp_err_t ret = ESP_OK;

    /* Stop the HTTP server if running */
    if (server) {
        ESP_LOGI(TAG, "Stopping HTTP Server");
        esp_err_t stop_ret = httpd_stop(server);
        if (stop_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop HTTP server: %s", esp_err_to_name(stop_ret));
            ret = stop_ret;
        }
        server = NULL;
    }

    /* Free allocated server data */
    if (server_data) {
        free(server_data);
        server_data = NULL;
    }

    if (server == NULL && server_data == NULL) {
        ESP_LOGI(TAG, "File server stopped successfully");
    } else {
        ESP_LOGW(TAG, "File server may not have stopped completely");
    }

    return ret;
}
