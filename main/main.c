/*
 * main.c — chaosOS entry point.
 *
 * Bring up NVS (apps persist there), the board + LVGL, register the built-in
 * apps and hand control to the kernel. app_main then returns; the LVGL and
 * sensor tasks keep the system alive.
 */
#include "nvs_flash.h"
#include "esp_log.h"
#include "bsp.h"
#include "chaos_os.h"
#include "apps.h"

static const char *TAG = "chaos";

void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(bsp_init());

    chaos_apps_register_all();
    chaos_os_start();

    ESP_LOGI(TAG, "chaosOS running — welcome to the machine");
}
