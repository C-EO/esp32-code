#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us

#include "config.h"
#include "farmsentry_types.h"
#include "sensor_task.h"

static const char *TAG = "sensor_task";

// ------------------------------------------------------------------
// DHT22 driver (bit-banged, no external component required)
// ------------------------------------------------------------------
// Protocol: MCU pulls line low 1-10ms, releases, sensor responds
// with a low+high handshake, then streams 40 bits (5 bytes: humidity
// hi/lo, temp hi/lo, checksum). Each bit is a fixed-width low pulse
// followed by a variable-width high pulse (~26-28us = 0, ~70us = 1).
static bool dht22_wait_level(gpio_num_t pin, int level, uint32_t timeout_us) {
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pin) != level) {
        if (esp_timer_get_time() - start > timeout_us) {
            return false;
        }
    }
    return true;
}

static bool dht22_read(float *out_temp_c, float *out_humidity_pct) {
    uint8_t data[5] = {0};

    // 1. Send start signal: pull low >=1ms, release, wait for response.
    gpio_set_direction(DHT22_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT22_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(DHT22_PIN, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(DHT22_PIN, GPIO_MODE_INPUT);

    // 2. Sensor pulls low ~80us, then high ~80us (its own ack).
    portDISABLE_INTERRUPTS();
    if (!dht22_wait_level(DHT22_PIN, 0, 100)) { portENABLE_INTERRUPTS(); ESP_LOGW(TAG, "no ack (low)"); return false; }
    if (!dht22_wait_level(DHT22_PIN, 1, 100)) { portENABLE_INTERRUPTS(); ESP_LOGW(TAG, "no ack (high)"); return false; }
    if (!dht22_wait_level(DHT22_PIN, 0, 100)) { portENABLE_INTERRUPTS(); ESP_LOGW(TAG, "no data start"); return false; }

    // 3. Read 40 bits.
    for (int i = 0; i < 40; i++) {
        if (!dht22_wait_level(DHT22_PIN, 1, 100)) { portENABLE_INTERRUPTS(); ESP_LOGW(TAG, "bit %d timeout(low)", i); return false; }

        int64_t high_start = esp_timer_get_time();
        if (!dht22_wait_level(DHT22_PIN, 0, 100)) { portENABLE_INTERRUPTS(); ESP_LOGW(TAG, "bit %d timeout(high)", i); return false; }
        int64_t high_us = esp_timer_get_time() - high_start;

        data[i / 8] <<= 1;
        if (high_us > 40) {          // ~70us => 1, ~26-28us => 0
            data[i / 8] |= 1;
        }
    }
    portENABLE_INTERRUPTS();

    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        ESP_LOGW(TAG, "checksum mismatch (%02x vs %02x)", checksum, data[4]);
        return false;
    }

    int16_t raw_humidity = (data[0] << 8) | data[1];
    int16_t raw_temp     = (data[2] << 8) | data[3];
    bool negative = raw_temp & 0x8000;
    raw_temp &= 0x7FFF;

    *out_humidity_pct = raw_humidity / 10.0f;
    *out_temp_c        = negative ? -(raw_temp / 10.0f) : (raw_temp / 10.0f);
    return true;
}

// ------------------------------------------------------------------
// Ultrasonic (HC-SR04-style) feed-level driver
// ------------------------------------------------------------------
static bool ultrasonic_read_cm(float *out_cm) {
    gpio_set_level(ULTRASONIC_TRIG_PIN, 0);
    esp_rom_delay_us(2);
    gpio_set_level(ULTRASONIC_TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(ULTRASONIC_TRIG_PIN, 0);

    // Wait for echo to go high (start of pulse), then measure its width.
    int64_t timeout_start = esp_timer_get_time();
    while (gpio_get_level(ULTRASONIC_ECHO_PIN) == 0) {
        if (esp_timer_get_time() - timeout_start > 30000) { // 30ms ~= no echo
            ESP_LOGW(TAG, "ultrasonic: no echo start");
            return false;
        }
    }
    int64_t pulse_start = esp_timer_get_time();

    while (gpio_get_level(ULTRASONIC_ECHO_PIN) == 1) {
        if (esp_timer_get_time() - pulse_start > 30000) {
            ESP_LOGW(TAG, "ultrasonic: echo stuck high");
            return false;
        }
    }
    int64_t pulse_us = esp_timer_get_time() - pulse_start;

    // Speed of sound ~343 m/s -> distance(cm) = pulse_us / 58 (round trip).
    *out_cm = pulse_us / 58.0f;
    return true;
}

static float distance_to_level_pct(float distance_cm) {
    float span = HOPPER_EMPTY_CM - HOPPER_FULL_CM;
    if (span <= 0) return NAN;
    float pct = (HOPPER_EMPTY_CM - distance_cm) / span * 100.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

// ------------------------------------------------------------------
// Task
// ------------------------------------------------------------------
static void sensor_task(void *pvParameters) {
    QueueHandle_t *queues = (QueueHandle_t *)pvParameters; // [0]=relay, [1]=wifi
    QueueHandle_t relay_queue = queues[0];
    QueueHandle_t wifi_queue  = queues[1];

    gpio_set_direction(ULTRASONIC_TRIG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(ULTRASONIC_ECHO_PIN, GPIO_MODE_INPUT);
    gpio_set_level(ULTRASONIC_TRIG_PIN, 0);

    for (;;) {
        sensor_reading_t reading = {0};
        reading.timestamp_us = esp_timer_get_time();

        float temp_c = NAN, humidity_pct = NAN;
        reading.dht_valid = dht22_read(&temp_c, &humidity_pct);
        reading.temperature_c = temp_c;
        reading.humidity_pct = humidity_pct;

        if (reading.dht_valid) {
            ESP_LOGI(TAG, "DHT22: %.1fC  %.1f%%RH", temp_c, humidity_pct);
            if (temp_c > TEMP_HIGH_ALERT_C) {
                ESP_LOGW(TAG, "ALERT: coop temperature high (%.1fC)", temp_c);
            }
            if (humidity_pct > HUMIDITY_HIGH_ALERT_PCT) {
                ESP_LOGW(TAG, "ALERT: coop humidity high (%.1f%%)", humidity_pct);
            }
        } else {
            ESP_LOGW(TAG, "DHT22 read failed this cycle");
        }

        float dist_cm = NAN;
        reading.ultrasonic_valid = ultrasonic_read_cm(&dist_cm);
        reading.hopper_distance_cm = dist_cm;
        if (reading.ultrasonic_valid) {
            reading.hopper_level_pct = distance_to_level_pct(dist_cm);
            ESP_LOGI(TAG, "Hopper: %.1fcm (%.0f%% full)", dist_cm, reading.hopper_level_pct);
            if (reading.hopper_level_pct < HOPPER_LOW_THRESHOLD_PCT) {
                ESP_LOGW(TAG, "ALERT: feed hopper low (%.0f%%)", reading.hopper_level_pct);
            }
        } else {
            reading.hopper_level_pct = NAN;
            ESP_LOGW(TAG, "Ultrasonic read failed this cycle");
        }

        // Non-blocking sends: a full queue (slow consumer) drops the
        // oldest-style — we just skip this cycle's push rather than
        // stall sensing.
        if (relay_queue) xQueueOverwrite(relay_queue, &reading);
        if (wifi_queue)  xQueueSend(wifi_queue, &reading, 0);

        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_INTERVAL_MS));
    }
}

void sensor_task_start(QueueHandle_t relay_queue, QueueHandle_t wifi_queue) {
    static QueueHandle_t queues[2];
    queues[0] = relay_queue;
    queues[1] = wifi_queue;
    xTaskCreate(sensor_task, "sensor_task", 4096, queues, 5, NULL);
}
