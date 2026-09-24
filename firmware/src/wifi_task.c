#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "config.h"
#include "farmsentry_types.h"
#include "wifi_task.h"

static const char *TAG = "wifi_task";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_RETRY_MAX) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d", s_retry_count, WIFI_RETRY_MAX);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Blocks until connected or WIFI_RETRY_MAX exhausted. Returns true on success.
static bool wifi_connect_blocking(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID '%s'...", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
        return true;
    }
    ESP_LOGE(TAG, "WiFi connect failed/timed out");
    return false;
}

static bool http_post_json(const char *json) {
    esp_http_client_config_t http_cfg = {
        .url = API_ENDPOINT,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);
    bool ok = false;
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ok = (status >= 200 && status < 300);
        ESP_LOGI(TAG, "POST status %d %s", status, ok ? "" : "(non-2xx)");
    } else {
        ESP_LOGE(TAG, "POST failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return ok;
}

static char *build_reading_json(const sensor_reading_t *r) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "node_id", NODE_ID);
    cJSON_AddStringToObject(root, "type", "reading");
    cJSON_AddNumberToObject(root, "timestamp_us", (double)r->timestamp_us);
    cJSON_AddBoolToObject(root, "dht_valid", r->dht_valid);
    if (r->dht_valid) {
        cJSON_AddNumberToObject(root, "temperature_c", r->temperature_c);
        cJSON_AddNumberToObject(root, "humidity_pct", r->humidity_pct);
    }
    cJSON_AddBoolToObject(root, "ultrasonic_valid", r->ultrasonic_valid);
    if (r->ultrasonic_valid) {
        cJSON_AddNumberToObject(root, "hopper_distance_cm", r->hopper_distance_cm);
        cJSON_AddNumberToObject(root, "hopper_level_pct", r->hopper_level_pct);
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_event_json(const relay_event_t *e) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "node_id", NODE_ID);
    cJSON_AddStringToObject(root, "type", "relay_event");
    cJSON_AddNumberToObject(root, "timestamp_us", (double)e->timestamp_us);
    cJSON_AddBoolToObject(root, "feed_triggered", e->feed_triggered);
    cJSON_AddBoolToObject(root, "water_triggered", e->water_triggered);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static void wifi_task(void *pvParameters) {
    QueueHandle_t *queues = (QueueHandle_t *)pvParameters; // [0]=sensor, [1]=event
    QueueHandle_t sensor_queue = queues[0];
    QueueHandle_t event_queue  = queues[1];

    bool connected = wifi_connect_blocking();
    if (!connected) {
        ESP_LOGW(TAG, "Proceeding without WiFi — readings will be logged locally only");
    }

    sensor_reading_t reading;
    relay_event_t event;

    for (;;) {
        // Drain whichever queue has data, non-blocking-ish with a
        // short timeout so both get serviced in turn.
        if (xQueueReceive(sensor_queue, &reading, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (connected) {
                char *json = build_reading_json(&reading);
                if (json) {
                    http_post_json(json);
                    free(json);
                }
            }
        }

        if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (connected) {
                char *json = build_event_json(&event);
                if (json) {
                    http_post_json(json);
                    free(json);
                }
            }
        }
    }
}

void wifi_task_start(QueueHandle_t sensor_queue, QueueHandle_t event_queue) {
    ESP_ERROR_CHECK(nvs_flash_init());

    static QueueHandle_t queues[2];
    queues[0] = sensor_queue;
    queues[1] = event_queue;
    xTaskCreate(wifi_task, "wifi_task", 6144, queues, 4, NULL);
}
