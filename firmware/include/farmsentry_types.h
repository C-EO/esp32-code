#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * One snapshot of everything the sensor task knows, handed off to
 * the relay task (for decisions) and the wifi task (for reporting).
 */
typedef struct {
    float temperature_c;     // NAN if last DHT22 read failed
    float humidity_pct;      // NAN if last DHT22 read failed
    bool  dht_valid;

    float hopper_distance_cm;
    float hopper_level_pct;  // 0-100, derived from distance + config.h geometry
    bool  ultrasonic_valid;

    int64_t timestamp_us;    // esp_timer_get_time() at read
} sensor_reading_t;

/*
 * One feeding/relay event, produced by the relay task, consumed by
 * the wifi task so it gets logged to the backend alongside sensor
 * data.
 */
typedef struct {
    bool feed_triggered;
    bool water_triggered;
    int64_t timestamp_us;
} relay_event_t;
