#pragma once

/*
 * FARMSENTRY — shared configuration
 * ----------------------------------
 * Pin numbers, network settings and thresholds live here so every
 * module (sensor, relay, wifi) pulls from one place. Edit this file
 * per-node if you deploy more than one ESP32 (one per coop section).
 */

// ---------- Identity ----------
// Give each physical node a unique id — the backend uses this to
// tell sections apart in the dashboard.
#define NODE_ID "section-1"

// ---------- Pin configuration ----------
// Avoids ESP32 boot-strapping pins (0, 2, 12, 15).
#define DHT22_PIN           GPIO_NUM_27   // temp/humidity sensor data pin
#define ULTRASONIC_TRIG_PIN GPIO_NUM_14   // feed-level sensor trigger
#define ULTRASONIC_ECHO_PIN GPIO_NUM_32   // feed-level sensor echo
#define RELAY_FEED_PIN      GPIO_NUM_26   // relay channel 1: feed motor/auger
#define RELAY_WATER_PIN     GPIO_NUM_25   // relay channel 2: water pump (if fitted)

// Most cheap relay modules are ACTIVE LOW (LOW = energized/ON).
// Flip to 0 if yours is active-high.
#define RELAY_ACTIVE_LOW    1

// ---------- Feed hopper geometry (for level % calc) ----------
// Distance in cm from the ultrasonic sensor to the bottom of the
// hopper (empty) and to a "full" hopper. Measure these once physically
// and update — the level percentage is derived from them.
#define HOPPER_EMPTY_CM      40.0f
#define HOPPER_FULL_CM       5.0f
#define HOPPER_LOW_THRESHOLD_PCT 20.0f   // trigger a refill/alert below this

// ---------- Feeding schedule ----------
#define FEED_INTERVAL_MS     (60UL * 60UL * 1000UL)  // hourly; shorten for demo
#define FEED_RUN_MS          3000UL                   // motor run time per cycle

// ---------- Sensor polling ----------
#define SENSOR_POLL_INTERVAL_MS 5000UL

// ---------- Environmental alert thresholds ----------
// Placeholder broiler coop targets — confirm with the research team
// before the final build.
#define TEMP_HIGH_ALERT_C     35.0f
#define HUMIDITY_HIGH_ALERT_PCT 80.0f

// ---------- WiFi ----------
// Do NOT commit real credentials. Override these via a local
// firmware/include/secrets.h (gitignored) that #defines the same
// macros — see secrets.h.example in this folder.
#ifdef __has_include
  #if __has_include("secrets.h")
    #include "secrets.h"
  #endif
#endif

#ifndef WIFI_SSID
#define WIFI_SSID       "CHANGE_ME"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD   "CHANGE_ME"
#endif

// ---------- Backend API ----------
#ifndef API_ENDPOINT
#define API_ENDPOINT    "http://CHANGE_ME/api/readings"
#endif

#define WIFI_CONNECT_TIMEOUT_MS   15000UL
#define WIFI_RETRY_MAX            5
