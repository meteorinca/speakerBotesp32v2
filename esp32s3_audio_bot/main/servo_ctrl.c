#include "common.h"
#include "esp_log.h"

static const char *TAG = "servo_ctrl";

void servo_ctrl_init(void) {
    ESP_LOGI(TAG, "servo init placeholder on pin %d", SERVO_PIN);
}

void servo_ctrl_set_angle(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    app_state_get()->servo_angle = angle;
    ESP_LOGI(TAG, "servo angle -> %d", angle);
}
