#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>

#include "common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_udp";
static bool g_udp_enabled = false;
static uint16_t g_udp_port = UDP_AUDIO_PORT;
static char g_codec[16] = "pcm";
static uint32_t g_sample_rate = 48000;
static uint16_t g_frame_ms = 20;
static uint32_t g_last_seq = 0;
static bool g_seen_first = false;

void audio_udp_set_stream(bool enabled, uint16_t udp_port, const char *codec, uint32_t sample_rate, uint16_t frame_ms) {
    g_udp_enabled = enabled;
    g_udp_port = udp_port;
    g_sample_rate = sample_rate;
    g_frame_ms = frame_ms;
    snprintf(g_codec, sizeof(g_codec), "%s", codec ? codec : "pcm");
}

static void udp_rx_task(void *arg) {
    uint8_t buffer[sizeof(audio_packet_header_t) + AUDIO_MAX_PAYLOAD];

    while (1) {
        if (!g_udp_enabled) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "socket create failed: errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = htonl(INADDR_ANY),
            .sin_port = htons(g_udp_port),
        };
        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            ESP_LOGE(TAG, "bind failed: errno=%d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "listening for audio on udp/%u codec=%s rate=%lu frame=%u",
                 g_udp_port, g_codec, (unsigned long)g_sample_rate, g_frame_ms);

        while (g_udp_enabled) {
            int len = recvfrom(sock, buffer, sizeof(buffer), 0, NULL, 0);
            if (len <= (int)sizeof(audio_packet_header_t)) {
                continue;
            }

            audio_packet_header_t header;
            memcpy(&header, buffer, sizeof(header));
            uint16_t magic = ntohs(header.magic);
            uint32_t seq = ntohl(header.seq);
            uint16_t payload_len = ntohs(header.payload_len);

            if (magic != AUDIO_PACKET_MAGIC || header.version != AUDIO_PACKET_VERSION) {
                continue;
            }
            if ((sizeof(header) + payload_len) > (size_t)len || payload_len > AUDIO_MAX_PAYLOAD) {
                continue;
            }

            app_state_t *state = app_state_get();
            state->rx_packets++;
            if (g_seen_first && seq > g_last_seq + 1) {
                state->lost_packets += (seq - g_last_seq - 1);
            }
            g_seen_first = true;
            g_last_seq = seq;

            const uint8_t *payload = buffer + sizeof(header);
            if (header.type == AUDIO_PACKET_TYPE_PCM) {
                audio_pipeline_push_pcm(payload, payload_len);
            } else {
                // Opus hook point: decode and forward to playback ring buffer.
            }
        }

        close(sock);
    }
}

void audio_pipeline_start(void);

void __attribute__((constructor)) audio_udp_constructor(void) {
    xTaskCreate(udp_rx_task, "udp_rx_task", 4096, NULL, 5, NULL);
}
