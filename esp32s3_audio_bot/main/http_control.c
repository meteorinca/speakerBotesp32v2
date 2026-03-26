#include <stdio.h>
#include <string.h>

#include "common.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <sys/param.h>  


static const char *TAG = "http_control";

static esp_err_t send_json(httpd_req_t *req, cJSON *json) {
    const char *body = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body);
    cJSON_free((void *)body);
    cJSON_Delete(json);
    return err;
}

static cJSON *read_json_body(httpd_req_t *req) {
    char buffer[256] = {0};
    int total = httpd_req_recv(req, buffer, MIN(req->content_len, sizeof(buffer) - 1));
    if (total <= 0) {
        return NULL;
    }
    return cJSON_Parse(buffer);
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    app_state_t *state = app_state_get();
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "ip", state->ip);
    cJSON_AddBoolToObject(json, "streaming", state->streaming);
    cJSON_AddStringToObject(json, "codec", state->codec);
    cJSON_AddNumberToObject(json, "sample_rate", state->sample_rate);
    cJSON_AddNumberToObject(json, "frame_ms", state->frame_ms);
    cJSON_AddNumberToObject(json, "udp_port", state->udp_port);
    cJSON_AddNumberToObject(json, "rx_packets", state->rx_packets);
    cJSON_AddNumberToObject(json, "lost_packets", state->lost_packets);
    cJSON_AddNumberToObject(json, "underruns", state->underruns);
    cJSON_AddNumberToObject(json, "servo_angle", state->servo_angle);
    cJSON_AddStringToObject(json, "oled_line1", state->oled_line1);
    cJSON_AddStringToObject(json, "oled_line2", state->oled_line2);
    return send_json(req, json);
}

static esp_err_t servo_post_handler(httpd_req_t *req) {
    cJSON *json = read_json_body(req);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }
    cJSON *angle = cJSON_GetObjectItem(json, "angle");
    int value = angle ? angle->valueint : 90;
    app_state_t *state = app_state_get();
    state->servo_angle = value;
    servo_ctrl_set_angle(value);
    cJSON_Delete(json);
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "angle", value);
    return send_json(req, out);
}

static esp_err_t oled_post_handler(httpd_req_t *req) {
    cJSON *json = read_json_body(req);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }
    app_state_t *state = app_state_get();
    const cJSON *line1 = cJSON_GetObjectItem(json, "line1");
    const cJSON *line2 = cJSON_GetObjectItem(json, "line2");
    snprintf(state->oled_line1, sizeof(state->oled_line1), "%s", cJSON_IsString(line1) ? line1->valuestring : "");
    snprintf(state->oled_line2, sizeof(state->oled_line2), "%s", cJSON_IsString(line2) ? line2->valuestring : "");
    oled_ui_set_text(state->oled_line1, state->oled_line2);
    cJSON_Delete(json);
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    return send_json(req, out);
}

static esp_err_t stream_start_post_handler(httpd_req_t *req) {
    cJSON *json = read_json_body(req);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }
    app_state_t *state = app_state_get();
    cJSON *udp_port = cJSON_GetObjectItem(json, "udp_port");
    cJSON *sample_rate = cJSON_GetObjectItem(json, "sample_rate");
    cJSON *frame_ms = cJSON_GetObjectItem(json, "frame_ms");
    cJSON *codec = cJSON_GetObjectItem(json, "codec");
    state->udp_port = udp_port ? udp_port->valueint : UDP_AUDIO_PORT;
    state->sample_rate = sample_rate ? sample_rate->valueint : 48000;
    state->frame_ms = frame_ms ? frame_ms->valueint : 20;
    snprintf(state->codec, sizeof(state->codec), "%s", cJSON_IsString(codec) ? codec->valuestring : "pcm");
    state->streaming = true;
    audio_udp_set_stream(true, state->udp_port, state->codec, state->sample_rate, state->frame_ms);
    oled_ui_set_text(state->ip, "Streaming");
    cJSON_Delete(json);
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    return send_json(req, out);
}

static esp_err_t stream_stop_post_handler(httpd_req_t *req) {
    app_state_t *state = app_state_get();
    state->streaming = false;
    audio_udp_set_stream(false, state->udp_port, state->codec, state->sample_rate, state->frame_ms);
    oled_ui_set_text(state->ip, "Idle");
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", true);
    return send_json(req, out);
}

static esp_err_t mic_get_handler(httpd_req_t *req) {
    int amp = audio_mic_get_amplitude();
    cJSON *out = cJSON_CreateObject();
    cJSON_AddNumberToObject(out, "amplitude", amp);
    return send_json(req, out);
}

void http_control_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start http server");
        return;
    }

    httpd_uri_t routes[] = {
        {.uri = "/status", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = NULL},
        {.uri = "/servo", .method = HTTP_POST, .handler = servo_post_handler, .user_ctx = NULL},
        {.uri = "/oled", .method = HTTP_POST, .handler = oled_post_handler, .user_ctx = NULL},
        {.uri = "/stream/start", .method = HTTP_POST, .handler = stream_start_post_handler, .user_ctx = NULL},
        {.uri = "/stream/stop", .method = HTTP_POST, .handler = stream_stop_post_handler, .user_ctx = NULL},
        {.uri = "/mic", .method = HTTP_GET, .handler = mic_get_handler, .user_ctx = NULL},
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    ESP_LOGI(TAG, "http control API ready");
}
