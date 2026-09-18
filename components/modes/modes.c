/*
 * modes.c — the whole operating system: three modes and a frame loop.
 */
#include "mode.h"
#include "bsp.h"
#include "fx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "chaos";

static const chaos_mode_t *const s_modes[] = {
    &mode_sand,      /* boots here */
    &mode_rpg,
    &mode_glitch,
};
#define MODE_COUNT (sizeof(s_modes) / sizeof(s_modes[0]))

static int s_current;

/* A brief colour wash so a mode change is unmistakable without any UI. */
static void mode_splash(uint32_t colour)
{
    fx_clear(bsp_fb(), colour);
    bsp_present();
    vTaskDelay(pdMS_TO_TICKS(90));
}

static void switch_to(int idx)
{
    if (s_modes[s_current]->leave) s_modes[s_current]->leave();
    s_current = idx;
    ESP_LOGI(TAG, "mode -> %s", s_modes[s_current]->name);

    static const uint8_t tint[][3] = {
        { 40, 30, 10 },   /* sand   */
        { 10, 30, 40 },   /* rpg    */
        { 40, 10, 40 },   /* glitch */
    };
    mode_splash(fx_rgb(tint[idx][0], tint[idx][1], tint[idx][2]));

    if (s_modes[s_current]->enter) s_modes[s_current]->enter();
}

void chaos_run(void)
{
    fx_srand((uint32_t)esp_timer_get_time());

    s_current = 0;
    ESP_LOGI(TAG, "boot -> %s", s_modes[s_current]->name);
    if (s_modes[s_current]->enter) s_modes[s_current]->enter();

    uint32_t last = (uint32_t)(esp_timer_get_time() / 1000);

    for (;;) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        uint32_t dt  = now - last;
        last = now;
        if (dt > 120) dt = 120;        /* don't let a stall explode the sim */

        if (bsp_button_pressed(BSP_BTN_MODE)) {
            switch_to((s_current + 1) % MODE_COUNT);
            continue;
        }
        if (bsp_button_pressed(BSP_BTN_ACTION) && s_modes[s_current]->action) {
            s_modes[s_current]->action();
        }

        s_modes[s_current]->tick(dt);
        bsp_present();

        /* Yield so the idle task can feed the watchdog even when a mode is
         * rendering flat out. */
        vTaskDelay(1);
    }
}
