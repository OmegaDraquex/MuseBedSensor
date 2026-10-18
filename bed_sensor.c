/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bed_sensor.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

// ---- State machine (host-tested) --------------------------------------------

// Out of bed only after a full minute continuously below threshold.
#define BED_OUT_DWELL_MS 60000

typedef struct {
    bool occupied;
    bool initialized;
    uint32_t below_since_ms;  // first below-threshold sample of this dip, else 0
    uint32_t changed_at_ms;
    int raw;
} bed_state_t;

// Fold one averaged sample in. Above threshold counts as in bed at once;
// below starts (or continues) the out dwell. Times are monotonic
// milliseconds and wrap-safe. Returns true when occupancy changed.
static bool bed_update(bed_state_t *s, uint32_t now_ms, int raw, int threshold) {
    s->raw = raw;
    if (!s->initialized) {
        s->initialized = true;
        s->changed_at_ms = now_ms;
    }
    if (raw > threshold) {
        s->below_since_ms = 0;
        if (!s->occupied) {
            s->occupied = true;
            s->changed_at_ms = now_ms;
            return true;
        }
        return false;
    }
    if (!s->occupied) return false;
    if (s->below_since_ms == 0) s->below_since_ms = now_ms;
    if ((int32_t)(now_ms - s->below_since_ms) >= (int32_t)BED_OUT_DWELL_MS) {
        s->occupied = false;
        s->changed_at_ms = now_ms;
        s->below_since_ms = 0;
        return true;
    }
    return false;
}

// bed.occupancy result. stable_s is how long the state has held.
static cJSON *bed_result(bool occupied, int raw, int threshold, uint32_t stable_s) {
    cJSON *result = cJSON_CreateObject();
    cJSON *payload = cJSON_CreateObject();
    if (!result || !payload) {
        cJSON_Delete(result);
        cJSON_Delete(payload);
        return NULL;
    }
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddBoolToObject(payload, "occupied", occupied);
    cJSON_AddNumberToObject(payload, "raw", raw);
    cJSON_AddNumberToObject(payload, "threshold", threshold);
    cJSON_AddNumberToObject(payload, "stable_s", (double)stable_s);
    cJSON_AddItemToObject(result, "payload", payload);
    return result;
}

// ---- Sampler ----------------------------------------------------------------

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "stack_monitor.h"

#include "config_store.h"

static const char *TAG = "link.bed";

#define BED_ADC_GPIO   CONFIG_HOMEHUB_BED_ADC_GPIO
#define BED_SAMPLES    8
#define BED_PERIOD_MS  1000

#define BED_THRESHOLD_KEY "bed_threshold"
// The console accepts the middle of the 12-bit range; the ends are a broken
// wire (near 0) or a saturated divider (near 4095), not a calibration.
#define BED_THRESHOLD_MIN 500
#define BED_THRESHOLD_MAX 4000

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bed_state_t s_state;
static int s_threshold = CONFIG_HOMEHUB_BED_THRESHOLD;
static bool s_have_reading;

static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_channel;

static void bed_task(void *arg) {
    (void)arg;
    stack_monitor_t stack = STACK_MONITOR_INIT;
    for (;;) {
        long sum = 0;
        int n = 0;
        for (int i = 0; i < BED_SAMPLES; i++) {
            int raw = 0;
            if (adc_oneshot_read(s_adc, s_channel, &raw) == ESP_OK) {
                sum += raw;
                n++;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (n > 0) {
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            bool changed;
            taskENTER_CRITICAL(&s_lock);
            changed = bed_update(&s_state, now_ms, (int)(sum / n), s_threshold);
            s_have_reading = true;
            taskEXIT_CRITICAL(&s_lock);
            if (changed) {
                ESP_LOGI(TAG, "%s: raw=%d threshold=%d",
                         s_state.occupied ? "in bed" : "out of bed",
                         s_state.raw, s_threshold);
            }
        }
        stack_monitor_poll(&stack);
        vTaskDelay(pdMS_TO_TICKS(BED_PERIOD_MS));
    }
}

static int bed_load_threshold(void) {
    char stored[8];
    if (!config_get_str(BED_THRESHOLD_KEY, stored, sizeof(stored))) {
        return CONFIG_HOMEHUB_BED_THRESHOLD;
    }
    char *end = NULL;
    long v = strtol(stored, &end, 10);
    if (end == stored || *end != '\0' || v < BED_THRESHOLD_MIN || v > BED_THRESHOLD_MAX) {
        ESP_LOGW(TAG, "ignoring an invalid stored threshold");
        return CONFIG_HOMEHUB_BED_THRESHOLD;
    }
    return (int)v;
}

void bed_sensor_init(void) {
    s_threshold = bed_load_threshold();
    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(BED_ADC_GPIO, &unit, &s_channel) != ESP_OK ||
        unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO%d is not an ADC1 pin", BED_ADC_GPIO);
        return;
    }
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed");
        return;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_oneshot_config_channel(s_adc, s_channel, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel init failed");
        return;
    }
    if (xTaskCreate(bed_task, "bed", 3072, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start the sensor task");
        return;
    }
    ESP_LOGI(TAG, "reading the FSR on GPIO%d (ADC1 channel %d), threshold=%d",
             BED_ADC_GPIO, (int)s_channel, s_threshold);
}

cJSON *bed_sensor_command(void) {
    bed_state_t snapshot;
    int threshold;
    bool have_reading;
    taskENTER_CRITICAL(&s_lock);
    snapshot = s_state;
    threshold = s_threshold;
    have_reading = s_have_reading;
    taskEXIT_CRITICAL(&s_lock);
    if (!have_reading) {
        cJSON *result = cJSON_CreateObject();
        if (!result) return NULL;
        cJSON_AddBoolToObject(result, "ok", false);
        cJSON *error = cJSON_AddObjectToObject(result, "error");
        cJSON_AddStringToObject(error, "code", "no_readings");
        cJSON_AddStringToObject(error, "message", "no FSR reading yet");
        return result;
    }
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return bed_result(snapshot.occupied, snapshot.raw, threshold,
                      (now_ms - snapshot.changed_at_ms) / 1000);
}

bool bed_sensor_console_command(const char *line) {
    if (strcmp(line, "bed_status") == 0) {
        bed_state_t snapshot;
        int threshold;
        taskENTER_CRITICAL(&s_lock);
        snapshot = s_state;
        threshold = s_threshold;
        taskEXIT_CRITICAL(&s_lock);
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        printf("@bed_status occupied=%d raw=%d threshold=%d stable_s=%lu\n",
               snapshot.occupied ? 1 : 0, snapshot.raw, threshold,
               (unsigned long)((now_ms - snapshot.changed_at_ms) / 1000));
        fflush(stdout);
        return true;
    }
    static const char prefix[] = "bed_threshold=";
    if (strncmp(line, prefix, strlen(prefix)) != 0) return false;
    char *end = NULL;
    long v = strtol(line + strlen(prefix), &end, 10);
    if (end == line + strlen(prefix) || *end != '\0' ||
        v < BED_THRESHOLD_MIN || v > BED_THRESHOLD_MAX) {
        ESP_LOGW(TAG, "not a threshold (500..4000)");
        printf("@bed_threshold error invalid\n");
        fflush(stdout);
        return true;
    }
    char stored[8];
    snprintf(stored, sizeof(stored), "%ld", v);
    if (!config_set_str(BED_THRESHOLD_KEY, stored)) {
        ESP_LOGE(TAG, "couldn't store the threshold");
        printf("@bed_threshold error storage\n");
        fflush(stdout);
        return true;
    }
    taskENTER_CRITICAL(&s_lock);
    s_threshold = (int)v;
    // A new threshold restarts any dip in progress under the new line.
    s_state.below_since_ms = 0;
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "threshold=%ld", v);
    printf("@bed_threshold ok\n");
    fflush(stdout);
    return true;
}
