#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "config.h"
#include "farmsentry_types.h"
#include "relay_task.h"

static const char *TAG = "relay_task";

static inline void relay_write(gpio_num_t pin, bool on) {
    bool level = RELAY_ACTIVE_LOW ? !on : on;
    gpio_set_level(pin, level ? 1 : 0);
}

static void run_feed_motor(void) {
    ESP_LOGI(TAG, "Feed cycle start");
    relay_write(RELAY_FEED_PIN, true);
    vTaskDelay(pdMS_TO_TICKS(FEED_RUN_MS));
    relay_write(RELAY_FEED_PIN, false);
    ESP_LOGI(TAG, "Feed cycle complete");
}

static void relay_task(void *pvParameters) {
    QueueHandle_t *queues = (QueueHandle_t *)pvParameters; // [0]=sensor, [1]=event
    QueueHandle_t sensor_queue = queues[0];
    QueueHandle_t event_queue  = queues[1];

    gpio_set_direction(RELAY_FEED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY_WATER_PIN, GPIO_MODE_OUTPUT);
    relay_write(RELAY_FEED_PIN, false);
    relay_write(RELAY_WATER_PIN, false);

    TickType_t last_scheduled_feed = xTaskGetTickCount();
    const TickType_t feed_interval_ticks = pdMS_TO_TICKS(FEED_INTERVAL_MS);

    for (;;) {
        sensor_reading_t reading;
        bool have_reading = (xQueuePeek(sensor_queue, &reading, pdMS_TO_TICKS(1000)) == pdTRUE);

        bool low_hopper = have_reading && reading.ultrasonic_valid &&
                           reading.hopper_level_pct < HOPPER_LOW_THRESHOLD_PCT;
        bool schedule_due = (xTaskGetTickCount() - last_scheduled_feed) >= feed_interval_ticks;

        if (schedule_due || low_hopper) {
            if (low_hopper) {
                ESP_LOGW(TAG, "Triggering feed early: hopper at %.0f%%", reading.hopper_level_pct);
            }
            run_feed_motor();
            last_scheduled_feed = xTaskGetTickCount();

            relay_event_t event = {
                .feed_triggered = true,
                .water_triggered = false,
                .timestamp_us = esp_timer_get_time(),
            };
            if (event_queue) xQueueSend(event_queue, &event, 0);
        }

        // Poll roughly every second so a low-hopper reading gets
        // acted on promptly without busy-spinning.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void relay_task_start(QueueHandle_t sensor_queue, QueueHandle_t event_queue) {
    static QueueHandle_t queues[2];
    queues[0] = sensor_queue;
    queues[1] = event_queue;
    xTaskCreate(relay_task, "relay_task", 4096, queues, 5, NULL);
}
