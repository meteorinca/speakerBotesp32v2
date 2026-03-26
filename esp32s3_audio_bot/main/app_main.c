#include <string.h>

#include "common.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";
static app_state_t g_state = {
    .streaming = false,
    .udp_port = UDP_AUDIO_PORT,
    .sample_rate = 48000,
    .frame_ms = 20,
    .codec = "pcm",
    .servo_angle = 90,
    .rx_packets = 0,
    .lost_packets = 0,
    .underruns = 0,
    .oled_line1 = "Booting...",
    .oled_line2 = "speaker bot",
    .ip = "0.0.0.0",
};

app_state_t *app_state_get(void) {
    return &g_state;
}

void app_main(void) {
    ESP_LOGI(TAG, "speaker bot starting");
    servo_ctrl_init();
    servo_ctrl_set_angle(g_state.servo_angle);
    oled_ui_init();
    oled_ui_set_text(g_state.oled_line1, g_state.oled_line2);
    audio_i2s_init();
    audio_pipeline_start();
    wifi_sta_start();
    http_control_start();

    while (1) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        static uint32_t last_ui_state = 0; // 0: IP, 1: text, 2: eyes
        static bool force_update = true;
        uint32_t target_ui = 0;

        if (g_state.last_text_req > 0 && (now - g_state.last_text_req < 30000)) {
            target_ui = 1; // Text mode
        } else if (g_state.last_status_req > 0 && (now - g_state.last_status_req < 5000)) {
            target_ui = 2; // Eyes mode
        } else {
            target_ui = 0; // IP as usual
            if (strcmp(g_state.oled_line1, g_state.ip) != 0) {
                snprintf(g_state.oled_line1, sizeof(g_state.oled_line1), "%s", g_state.ip);
                snprintf(g_state.oled_line2, sizeof(g_state.oled_line2), "Idle");
                force_update = true; // ensure visual update
            }
        }

        if (target_ui != last_ui_state || force_update) {
            if (target_ui == 1 || target_ui == 0) {
                oled_ui_set_text(g_state.oled_line1, g_state.oled_line2);
            } else if (target_ui == 2) {
                oled_ui_draw_eyes();
            }
            last_ui_state = target_ui;
            force_update = false;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
