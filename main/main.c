/*
 * main.c — chaosOS entry point.
 *
 * Bring up NVS (apps persist there), the board + LVGL, register the built-in
 * apps and hand control to the kernel. app_main then returns; the LVGL and
 * sensor tasks keep the system alive.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "bsp.h"
#include "chaos_os.h"
#include "apps.h"

static const char *TAG = "chaos";

void app_main(void)
{
    /* The native-USB console re-enumerates on every reset, so the host needs a
     * moment to reopen the port. Without this pause the boot banner and any
     * bring-up errors scroll past before `screen` reconnects. Safe to delete
     * once hardware bring-up is finished. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "=== chaosOS booting ===");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    esp_err_t err = bsp_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board bring-up FAILED: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "most likely a wrong pin/address in components/bsp/include/bsp_pins.h");
        /* Idle rather than panic: a reboot loop on a native-USB board makes the
         * serial port disappear and reappear, so the error is never readable. */
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGE(TAG, "halted after failed board init");
        }
    }

    chaos_apps_register_all();
    chaos_os_start();

    ESP_LOGI(TAG, "chaosOS running — welcome to the machine");
}
