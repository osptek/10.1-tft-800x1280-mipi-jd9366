/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "modern_upload_page.h"

size_t get_modern_upload_html_size(void)
{
    return modern_upload_html_end - modern_upload_html_start;
}

size_t get_modern_upload_css_size(void)
{
    return modern_upload_css_end - modern_upload_css_start;
}

size_t get_modern_upload_js_size(void)
{
    return modern_upload_js_end - modern_upload_js_start;
}
