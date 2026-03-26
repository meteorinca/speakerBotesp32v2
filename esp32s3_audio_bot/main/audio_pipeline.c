#include <string.h>

#include "common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

static const char *TAG = "audio_pipeline";
static RingbufHandle_t g_pcm_play_rb = NULL;

void audio_pipeline_push_pcm(const uint8_t *data, uint16_t len) {
    if (!g_pcm_play_rb || !data || !len) {
        return;
    }
    if (xRingbufferSend(g_pcm_play_rb, data, len, 0) != pdTRUE) {
        app_state_t *state = app_state_get();
        state->underruns++;
    }
}

static void i2s_tx_task(void *arg) {
    size_t item_size = 0;
    bool was_playing = false;
    while (1) {
        uint8_t *item = (uint8_t *)xRingbufferReceive(g_pcm_play_rb, &item_size, pdMS_TO_TICKS(100));
        if (item) {
            audio_i2s_play_pcm(item, item_size);
            vRingbufferReturnItem(g_pcm_play_rb, item);
            was_playing = true;
        } else if (was_playing) {
            // Flush DMA buffers with zeroes to prevent DC offset scratch/buzz sounds
            uint8_t *silence = calloc(1, 4096);
            if (silence) {
                audio_i2s_play_pcm(silence, 4096);
                free(silence);
            }
            was_playing = false;
        }
    }
}

void audio_pipeline_start(void) {
    if (!g_pcm_play_rb) {
        g_pcm_play_rb = xRingbufferCreate(16 * 1024, RINGBUF_TYPE_BYTEBUF);
        xTaskCreate(i2s_tx_task, "i2s_tx_task", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "audio pipeline ready");
    }
}
