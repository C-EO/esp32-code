#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
 * Connects to WiFi (blocking, with retry) then starts a task that
 * drains sensor_reading_t values from sensor_queue and relay_event_t
 * values from event_queue, and POSTs each as JSON to API_ENDPOINT.
 * Call this once from app_main after the other tasks are created.
 */
void wifi_task_start(QueueHandle_t sensor_queue, QueueHandle_t event_queue);
