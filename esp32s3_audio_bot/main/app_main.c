#include <string.h>

#include "common.h"
#include "esp_log.h"
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
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
