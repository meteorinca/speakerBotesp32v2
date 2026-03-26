#include "common.h"
#include "esp_log.h"
#include "driver/ledc.h"

static const char *TAG = "servo_ctrl";

void servo_ctrl_init(void) {
    ESP_LOGI(TAG, "Initializing servo on pin %d", SERVO_PIN);
    
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_13_BIT,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 50,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {
        .gpio_num       = SERVO_PIN,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER_0,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&ch_conf);
}

void servo_ctrl_set_angle(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    app_state_get()->servo_angle = angle;
    
    // 50Hz (20ms period) -> 13-bit resol: 8192 units.
    // 0 deg: ~0.5ms -> (0.5/20.0) * 8192 = 205
    // 180 deg: ~2.5ms -> (2.5/20.0) * 8192 = 1024
    uint32_t duty = 205 + ((1024 - 205) * angle) / 180;
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    
    ESP_LOGI(TAG, "servo angle -> %d (duty: %d)", angle, (int)duty);
}
