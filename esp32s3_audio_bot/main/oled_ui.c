#include <stdio.h>
#include <string.h>

#include "common.h"
#include "esp_log.h"

static const char *TAG = "oled_ui";

void oled_ui_init(void) {
    ESP_LOGI(TAG, "OLED init placeholder on SDA=%d SCL=%d", OLED_SDA_PIN, OLED_SCL_PIN);
}

void oled_ui_set_text(const char *line1, const char *line2) {
    ESP_LOGI(TAG, "OLED: %s | %s", line1 ? line1 : "", line2 ? line2 : "");
}
