idf_component_register(
    SRCS
        "app_main.c"
        "wifi_sta.c"
        "http_control.c"
        "audio_udp.c"
        "audio_pipeline.c"
        "audio_i2s.c"
        "oled_ui.c"
        "servo_ctrl.c"
    INCLUDE_DIRS "."
    EMBED_FILES "geminieyes.bin"
    REQUIRES freertos cjson esp_http_server esp_event esp_wifi esp_netif esp_ringbuf esp_driver_ledc esp_driver_i2s esp_driver_i2c esp_timer nvs_flash
)
