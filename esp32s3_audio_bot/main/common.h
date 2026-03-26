#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "secrets.h"

#define LED_PIN 48
#define SERVO_PIN 18
#define OLED_SCL_PIN 42
#define OLED_SDA_PIN 41
#define MIC_SCK_PIN 5
#define MIC_WS_PIN 6
#define MIC_SD_PIN 7
#define SPK_BCLK_PIN 15
#define SPK_LRC_PIN 16
#define SPK_DIN_PIN 17

#define UDP_AUDIO_PORT 5006
#define AUDIO_PACKET_MAGIC 0xB07E
#define AUDIO_PACKET_VERSION 1
#define AUDIO_PACKET_TYPE_PCM 1
#define AUDIO_PACKET_TYPE_OPUS 2
#define AUDIO_MAX_PAYLOAD 2048

typedef struct {
    bool streaming;
    uint16_t udp_port;
    uint32_t sample_rate;
    uint16_t frame_ms;
    char codec[16];
    int servo_angle;
    uint32_t rx_packets;
    uint32_t lost_packets;
    uint32_t underruns;
    char oled_line1[32];
    char oled_line2[32];
    char ip[16];
    uint32_t last_status_req;
    uint32_t last_text_req;
} app_state_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint32_t seq;
    uint32_t timestamp_ms;
    uint16_t payload_len;
} audio_packet_header_t;

app_state_t *app_state_get(void);
void wifi_sta_start(void);
void http_control_start(void);
void audio_pipeline_start(void);
void audio_udp_set_stream(bool enabled, uint16_t udp_port, const char *codec, uint32_t sample_rate, uint16_t frame_ms);
void audio_pipeline_push_pcm(const uint8_t *data, uint16_t len);
void audio_i2s_init(void);
void audio_i2s_play_pcm(const uint8_t *data, size_t len);
int audio_mic_get_amplitude(void);
struct app_state_t;
void servo_ctrl_init(void);
void servo_ctrl_set_angle(int angle);
void oled_ui_init(void);
void oled_ui_set_text(const char *line1, const char *line2);
void oled_ui_draw_eyes(void);
