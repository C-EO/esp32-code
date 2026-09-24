#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "config.h"
#include "farmsentry_types.h"
#include "sensor_task.h"
#include "relay_task.h"
#include "wifi_task.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "FARMSENTRY firmware starting — node '%s'", NODE_ID);

    // sensor -> relay: length-1 "always freshest" queue. The relay
    // task peeks it, it never needs to see stale/queued-up readings.
    QueueHandle_t sensor_to_relay_queue = xQueueCreate(1, sizeof(sensor_reading_t));

    // sensor -> wifi: a real FIFO so a slow WiFi upload doesn't lose
    // readings (short backlog is fine; oldest is fine if it fills).
    QueueHandle_t sensor_to_wifi_queue = xQueueCreate(10, sizeof(sensor_reading_t));

    // relay -> wifi: feeding events to log to the backend.
    QueueHandle_t relay_event_queue = xQueueCreate(10, sizeof(relay_event_t));

    if (!sensor_to_relay_queue || !sensor_to_wifi_queue || !relay_event_queue) {
        ESP_LOGE(TAG, "Queue creation failed — halting");
        return;
    }

    sensor_task_start(sensor_to_relay_queue, sensor_to_wifi_queue);
    relay_task_start(sensor_to_relay_queue, relay_event_queue);
    wifi_task_start(sensor_to_wifi_queue, relay_event_queue);

    ESP_LOGI(TAG, "All tasks started");
}
