# FARMSENTRY firmware

ESP-IDF firmware (via PlatformIO) for one FARMSENTRY node. Reads a
DHT22 (temp/humidity) and an ultrasonic sensor (feed hopper level),
drives a relay-controlled feed motor, and reports readings + feeding
events to the backend over WiFi.

## Architecture

Three FreeRTOS tasks, connected by queues, started from `app_main` in `src/main.c`:

```
sensor_task  --[readings, length-1]-->  relay_task
    |                                        |
    +--[readings, FIFO]--+   +--[events]-----+
                          v   v
                       wifi_task --> HTTP POST (JSON) --> backend
```

- **`sensor_task.c`** — bit-banged DHT22 read + HC-SR04-style ultrasonic
  distance read, every `SENSOR_POLL_INTERVAL_MS` (see `include/config.h`).
- **`relay_task.c`** — fires the feed motor relay on a fixed interval,
  or early if the hopper reading drops below `HOPPER_LOW_THRESHOLD_PCT`.
- **`wifi_task.c`** — connects to WiFi once at boot, then drains both
  queues and POSTs each reading/event as JSON to `API_ENDPOINT`.

All tunables (pins, thresholds, intervals, WiFi/API config) live in
`include/config.h`.

## First-time setup

1. Copy `include/secrets.h.example` to `include/secrets.h` and fill in
   your real WiFi SSID/password and the backend API URL. This file is
   gitignored — never commit real credentials.
2. Open this `firmware/` folder directly in VS Code (not the repo root)
   so PlatformIO detects the project.
3. Let PlatformIO fetch the ESP-IDF toolchain on first build (~10-15 min).

## Simulate (no hardware needed)

`Ctrl+Shift+P` → `Wokwi: Start Simulator`. `diagram.json` wires up a
simulated ESP32 + DHT22 + HC-SR04 ultrasonic + relay module matching
the pins in `config.h`. WiFi/HTTP calls will fail inside the simulator
unless you configure Wokwi's simulated network — that's expected;
the point of the sim is to validate sensor/relay logic and serial
output before touching real hardware.

## Flash to real hardware

Connect the ESP32 via USB, then use PlatformIO's **Upload** button (or
`pio run --target upload`). Open the serial monitor (`pio device
monitor`, 115200 baud) to watch sensor readings and relay events.

## Known placeholders to revisit before demo

- `HOPPER_EMPTY_CM` / `HOPPER_FULL_CM` in `config.h` — measure against
  the real hopper geometry.
- `TEMP_HIGH_ALERT_C` / `HUMIDITY_HIGH_ALERT_PCT` — confirm real broiler
  coop targets with the research team.
- `API_ENDPOINT` JSON shape — confirm against whatever the PHP API
  actually expects (`build_reading_json` / `build_event_json` in
  `wifi_task.c` are the two payload shapes currently sent).
