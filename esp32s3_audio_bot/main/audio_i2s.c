#include "common.h"
#include "esp_log.h"

static const char *TAG = "audio_i2s";

void audio_i2s_init(void) {
    ESP_LOGI(TAG, "I2S init placeholder: configure STD TX on BCLK=%d LRC=%d DIN=%d",
             SPK_BCLK_PIN, SPK_LRC_PIN, SPK_DIN_PIN);
}

void audio_i2s_play_pcm(const uint8_t *data, size_t len) {
    (void)data;
    (void)len;
    // Hook point for i2s_channel_write(...) once hardware config is finalized.
}
