#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
 * Starts the relay task. It reads the latest sensor_reading_t from
 * sensor_queue (a length-1 "overwrite" queue — always the freshest
 * reading) and decides when to run the feed motor: on a fixed
 * interval, or immediately if the hopper level drops below
 * HOPPER_LOW_THRESHOLD_PCT. Every time it fires the relay, it pushes
 * a relay_event_t onto event_queue so the wifi task can log it.
 */
void relay_task_start(QueueHandle_t sensor_queue, QueueHandle_t event_queue);
