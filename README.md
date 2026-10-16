# Hydroponics firmware (ESP32-S3-WROOM-1 N16R8)

> **Target change (October 2026):** the board is now an ESP32-S3-WROOM-1 N16R8
> (16 MB flash, 8 MB octal PSRAM). This supersedes architecture decision **D02**
> ("WROOM-32D remains target"). `platformio.ini` uses `esp32-s3-devkitc-1`, and the env
> names are unchanged. Only the BME280 I2C pins have been remapped so far
> (SDA 21, SCL 47; the S3 has no GPIO22). The HC-SR04 (26/27) and ADC (32-35)
> pins in `board_profile.h` are **invalid on the S3** and must be remapped before
> WP2-WP6 (the S3's ADC1 is on GPIO1-10, and GPIO33-37 are taken by the PSRAM).

New layered FreeRTOS firmware. The design authority is
[`Firmware Architecture.pdf`](Firmware%20Architecture.pdf), Rev 1 (its source is
`Hydroponics-Monitoring-System-master/Documentation/Firmware Architecture.md`).
Section numbers (§) below refer to that document. The old team code is kept
unchanged and is used only as reference.

**Status: Stage 1 skeleton.** The shared contracts are in place and every
module exists as a stub that compiles. No sensor works yet. Nothing has been
built with PlatformIO or run on hardware yet (see "First checks").

## Language: C (migrated from C++, October 2026)

All project sources (`src/`, `include/`, `test/`) are C. The module layout, layer
rules, frozen-contract contents and env names are the same as in the C++ version.
Runtime behaviour is meant to be unchanged, but it has not yet been re-verified on
hardware (see "C migration build record" below). The pre-migration C++ tree was
not kept; its build numbers are in that record. The framework is still
Arduino-ESP32 on the same pinned platform, so the SDK settings below still apply.
The framework is C++ inside, but project code only uses its C APIs (`esp32-hal-*`,
FreeRTOS, `esp_timer`).

> **Design-authority conflict:** `Firmware Architecture.md` (Rev 1) still says to
> "retain PlatformIO, Arduino and C++" (§ on the baseline) and to "use .h/.cpp for
> the Arduino C++ application" (§5). This migration departs from those two
> sentences. Update the architecture document, or record the decision, at the next
> revision. `Stage 1 Report.html` also still describes the C++ code.

How the C++ constructs map to C:

| C++ | C |
|-----|-----|
| `namespace cfg` / `board` constants | `CFG_*` / `BOARD_*` macros with the original types, e.g. `CFG_BME280_PERIOD_MS`, `BOARD_PIN_I2C_SDA` |
| `enum class X : uint8_t` | `typedef uint8_t X;` plus prefixed constants (`SENSOR_*`, `METRIC_*`, `UNIT_*`, `HEALTH_*`, `DRIVER_*`, `CAPTURE_*`, `POWER_MODE_*`, `TASK_ID_*`, `LOG_LEVEL_*`, ...). Records keep their C++ size. `reading.c` checks `sizeof(Reading) == 56` and `sizeof(Snapshot) == 528` at compile time. |
| `kSensorCount` / `kMetricCount` | `SENSOR_COUNT` / `METRIC_COUNT` |
| `cfg::taskSpec(id)` | `cfgTaskSpec(id)`, which returns `const TaskSpec*` |
| HAL classes (`I2cBus`, `OneWireBus`, `AdcChannel`, `PulseCapture`) | A struct plus `i2cBusBegin(&bus, ...)`-style functions. Initialise statics with `I2C_BUS_INIT`, `ONE_WIRE_BUS_INIT`, `ADC_CHANNEL_INIT` or `PULSE_CAPTURE_INIT`. Members ending in `_` are private. |
| `SensorDriver` abstract class | A struct holding a `const SensorDriverOps*` table of function pointers. Each driver struct embeds `SensorDriver base` as its **first** member. Callers use `sensorDriverInit(drv)`, `sensorDriverStart(drv, now)`, `sensorDriverPoll(drv, now)`, ... on `&driver.base`. |
| Driver constructor `Bme280Driver d(config)` | `bme280DriverConstruct(&d, &config)`. Call it before any other use. In bring-up mains it is the first line of `setup()`. |
| `TdsDriver::setWaterTemperature(r)` | `tdsDriverSetWaterTemperature(&tds, &r)` |
| References `const Reading&` | Pointers `const Reading*` |
| `Wire` (I2C) | `esp32-hal-i2c.h` on controller 0, with the same 5 ms per-transaction timeout (`I2C_BUS_TIMEOUT_MS`) |
| `Serial.printf` / `Serial.begin(115200)` | `printf` to the UART0 console. It is already at 115200 (`CONFIG_ESP_CONSOLE_UART_BAUDRATE`), so no begin call is needed. |
| `void setup()` / `void loop()` | Defined in C. `hal/arduino_entry.h` binds them to the C++ symbol names the Arduino core calls (`_Z5setupv`, `_Z4loopv`). Include it in every file that defines `setup()`/`loop()`. |

To write a new driver, copy the pattern in `drivers/bme280.c`. Write `static`
functions taking a `SensorDriver*`, put them in one `static const SensorDriverOps`
table, and set `self->base.ops` to that table in `<name>DriverConstruct()`.

`NimBLE-Arduino` and `ArduinoJson` stay pinned in `platformio.ini`, but both are C++
libraries and no project code uses them. From C, WP8 can use the NimBLE host C
API (`ble_gatts_*`, `ble_gap_*`) that NimBLE-Arduino bundles, and cJSON or `snprintf`
for frames.

## Layout (§5)

```text
firmware/
  platformio.ini            pinned build + test_<sensor> envs + native
  include/board_profile.h   pins, rails, dividers; unresolved = unassigned  [FROZEN]
  include/app_config.h      periods, budgets, queue sizes, task table        [FROZEN]
  src/main.c                bootstrap only; loop() blocks forever
  src/domain/               pure C: Reading model, alarms, calibration       [types.h, reading.h FROZEN]
  src/drivers/              sensor_driver.h contract + one pair per sensor   [sensor_driver.h FROZEN]
  src/hal/                  clock, I2C, 1-Wire, ADC, pulse capture, safe GPIO, arduino_entry.h
  src/services/             acquisition, safety, power, rail epoch, snapshot, config, supervisor
  src/transports/           wifi_mqtt, ble_transport, protocol (only place JSON is built)
  src/diagnostics/          logger, progress counters
  test/bringup/             bringup_harness.h + one <sensor>_main.c per sensor
  test/native/              host unit tests for src/domain
```

## Layer rules (do not break these)

- **Drivers** talk to hardware and report status. No MQTT, BLE or Wi-Fi includes. No task creation. No power-mode decisions. No relay writes. No hidden long delays: `start()` begins work and `poll()` returns `Pending` plus `nextPollMs`.
- **`src/domain/`** must not include `Arduino.h` or FreeRTOS headers. It has to compile on a PC (`env:native`).
- **Single owners:** acquisition owns the sensor buses, Safety owns the relay outputs, WiFiMqtt owns the MQTT client, and Configuration owns NVS.
- **Readers of sensor data** peek at the latest `Snapshot` with zero wait. They never wait for a measurement.
- **Health values:** a missing or invalid value is never reported as `0`. Use `Health` (`HEALTH_WARMING`, `HEALTH_TIMEOUT`, `HEALTH_UNPOWERED`, `HEALTH_UNCALIBRATED`, ...).
- **Timing:** use `monoNowMs()` for every age and deadline. Never use wall-clock time.

## Frozen contracts

These five files are shared by everyone:

- `board_profile.h`
- `app_config.h`
- `domain/types.h` (`Snapshot.readings[]` is indexed by `MetricId`)
- `domain/reading.h`
- `drivers/sensor_driver.h`

The C migration rewrote all five in C syntax: identifiers changed, but values,
fields and field order did not. The team lead should review that once.

To change one, open a short review with the team lead. Do not edit them inside a feature branch. Each change needs a hardware or measurement reason.

## Work packages

| WP | Scope | Files | Stage | Blocked by |
|----|-------|-------|-------|------------|
| WP1 | **BME280** + I2C HAL | `drivers/bme280.*`, `hal/i2c_bus.*`, `test/bringup/bme280_main.c` | 2 | — (**running on S3 hardware 8 Oct 2026: ID 0x60 @0x76, 250 ms cadence, took 11 ms; fault/recovery tests pending**) |
| WP2 | DS18B20 + 1-Wire HAL | `drivers/ds18b20.*`, `hal/onewire_bus.*` | 2 | — |
| WP3 | DFR0026 light + ADC HAL | `drivers/light.*`, `hal/adc_channel.*` | 2 | — |
| WP4 | HC-SR04 + pulse capture (RMT) | `drivers/hcsr04.*`, `hal/pulse_capture.*` | 3 | ECHO divider verified |
| WP5 | pH + TDS, calibration | `drivers/ph.*`, `drivers/tds.*`, `domain/calibration.*` | 3 | WP3 ADC HAL; pH ADC headroom (§3) |
| WP6 | MQ-7 heater/conditioning | `drivers/mq7.*` | 3 | module identity, heater access |
| WP7 | Acquisition scheduler, Safety, alarm rules, snapshot | `services/acquisition.*`, `services/safety.*`, `domain/alarm.*` | 4 | ≥1 working driver; thresholds |
| WP8 | Wi-Fi/MQTT, BLE, protocol, configuration, desktop app update | `transports/*`, `services/configuration.*` | 5 | WP7 snapshot |
| WP9 | Power ISR, rail epoch, lease handover | `services/power_coordinator.*`, `services/rail_state.*` | 6 | power-sense GPIO and conditioning |
| WP10 | Logger queue, supervisor, watchdogs | `diagnostics/*`, `services/supervisor.*` | 4 | — |

WP1 to WP3 and WP10 can start in parallel right away. A driver counts as done when it meets the "Completion evidence" column of the §14 table. Compiling is not enough.

## Commissioning gates (hardware, §18)

The following are still `BOARD_PIN_UNASSIGNED` or unknown. Do not guess them in code.

- External-power-present GPIO, its voltage conditioning and hysteresis
- 5 V rail feedback
- Relay GPIO, polarity, NO/NC contacts and the load-safe state. The relay part label also needs resolving (IRLZ440N vs IRF4905).
- MQ-7 heater control
- pH and light ADC headroom
- All trip and clear thresholds

## Build and test

Requires PlatformIO (VS Code extension or `pip install platformio`).

```bash
pio run -e esp32dev
```

```bash
pio run -e test_bme280 -t upload -t monitor
```

```bash
pio test -e native
```

Each `test_<sensor>` env links all production code but replaces `src/main.c`
with `test/bringup/<sensor>_main.c`. The harness runs the driver from a
core-1, priority-4 task (the same placement as AcquisitionNormal). It prints
every reading with its health and the measured start-to-ready time. While a
driver is still a stub, it prints `init -> NOT_IMPLEMENTED`.

The native env needs a host gcc on `PATH`.

If the first build fails:

- **`build_src_filter` rejects `../test/bringup/...`:** move the bring-up mains to `src/bringup/`, point the filters there, and add `-<bringup/>` to the `esp32dev` and `native` filters.
- **Toolchain or path errors:** the project path contains spaces. Try setting `PLATFORMIO_CORE_DIR` to a path without spaces. This is something to check, not a confirmed problem.

## Stage 1 build record (4 October 2026)

PlatformIO Core 6.2.0 on Windows. The first build downloads about 1 GB of toolchain and takes ~8 min. Later builds take ~1 min.

| Env | Result | RAM | Flash |
|-----|--------|-----|-------|
| `esp32dev` | SUCCESS | 22 080 B (6.7 %) | 267 893 B (20.4 %) |
| `test_bme280` … `test_mq7` (7 envs) | all SUCCESS | ~21.5 KB (6.6 %) | ~269.5 KB (20.6 %) |
| `native` | 6/6 tests passed | — | — |

- There are no compiler warnings from `src/` or `include/`.
- The `../test/bringup/` source filter works as written.
- The project path contains spaces, but the build ran without problems.
- These numbers are for the stubs. Expect them to grow once the drivers, NimBLE and Wi-Fi are linked in.

**Resolved packages:**
- `framework-arduinoespressif32` 3.20017.241212 (Arduino-ESP32 2.0.17)
- `toolchain-xtensa-esp32` 8.4.0+2021r2-patch5
- NimBLE-Arduino 2.5.1
- ArduinoJson 6.21.5

**Resolved SDK configuration** (`tools/sdk/esp32/sdkconfig`, checked against §13):

| Setting | Value | Consequence |
|---------|-------|-------------|
| `CONFIG_FREERTOS_HZ` | 1000 | 1 ms tick. `pdMS_TO_TICKS` is exact. |
| `CONFIG_FREERTOS_UNICORE` | not set | Dual core. "Low-power single core" means only application affinity (§13). |
| `CONFIG_PM_ENABLE` | not set | No automatic DFS or light sleep with this framework build (§13). |
| `CONFIG_BTDM_CTRL_PINNED_TO_CORE` | 0 | BT controller on core 0, as §6 assumes. NimBLE host affinity is still to be checked in WP8. |
| `CONFIG_ARDUINO_RUNNING_CORE` | 1 | `loop()` task on core 1. It blocks forever. |
| `CONFIG_ESP_TASK_WDT_TIMEOUT_S` | 5 | Matches `CFG_TASK_WATCHDOG_MS`. |

Not yet done: flashing to a real board. Put the folder under git so the team can work on one branch per WP.

## C migration build record (16 October 2026)

Every env was built twice on the same machine and platform: once from the C++ tree
just before the port (baseline) and once from the C tree.

| Env | C++ RAM / Flash (B) | C RAM / Flash (B) | Result |
|-----|---------------------|-------------------|--------|
| `esp32dev` | 19 540 / 274 009 | 18 828 / 237 345 | SUCCESS |
| `test_bme280` | 19 680 / 304 665 | 19 044 / 273 333 | SUCCESS |
| `test_ds18b20` | 18 956 / 275 693 | 18 236 / 238 881 | SUCCESS |
| `test_light` | 18 956 / 275 693 | 18 236 / 238 897 | SUCCESS |
| `test_hcsr04` | 18 956 / 275 705 | 18 244 / 238 889 | SUCCESS |
| `test_ph` | 18 988 / 275 697 | 18 276 / 238 905 | SUCCESS |
| `test_tds` | 19 036 / 275 705 | 18 324 / 238 921 | SUCCESS |
| `test_mq7` | 18 980 / 275 705 | 18 268 / 238 909 | SUCCESS |
| `native` | 6/6 passed | 6/6 passed | PASSED |

- About 0.7 KB less RAM and about 31-37 KB less flash. The C++ `Wire` and
  `HardwareSerial` objects are no longer linked: I2C now goes straight to
  `esp32-hal-i2c`, and console output goes through `printf`. Data records have not
  changed size (checked at compile time in `reading.c`).
- There are no compiler warnings from `src/`, `include/` or `test/`. The
  platform-independent sources also compile cleanly on the host with
  `-Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes`.
- The C port of the BME280 compensation (`compensateT/P/H`) was compared on the host
  against the original C++ code. It gave identical results for 200 000 random
  calibration and ADC inputs.
- In the linked images, `setup`/`loop` resolve to the C definitions.
- **Console differences from `Serial`:** in this framework's S3 sdkconfig, `printf`
  ends lines with CRLF (`CONFIG_NEWLIB_STDOUT_LINE_ENDING_CRLF`), where `Serial.printf`
  sent a bare `\n`. It also mirrors output to the native USB-Serial-JTAG port
  (`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG`). `pio device monitor` on UART0 at
  115200 is unaffected. Scripts that parse logs byte for byte should allow `\r\n`.
- **Not yet re-verified on hardware:** the BME280 bring-up on the S3 (WP1 above was
  measured with the C++ build) and the console output described above. Re-run
  `pio run -e test_bme280 -t upload -t monitor` before relying on the C build.
