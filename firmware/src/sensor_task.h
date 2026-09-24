#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
 * Starts the sensor task. It polls DHT22 (temp/humidity) and the
 * ultrasonic feed-level sensor every SENSOR_POLL_INTERVAL_MS, and
 * pushes a sensor_reading_t onto both output queues on every cycle
 * (one feeds the relay task's decision logic, the other feeds the
 * wifi task's reporting loop — separate queues so a slow/blocked
 * consumer on one side never stalls the other).
 */
void sensor_task_start(QueueHandle_t relay_queue, QueueHandle_t wifi_queue);
