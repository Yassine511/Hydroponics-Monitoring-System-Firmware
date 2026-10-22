# Hydroponics firmware (ESP32-S3-WROOM-1 N16R8)

> **Target (October 2026):** ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB octal
> PSRAM), decision **D16** (supersedes D02). `platformio.ini` uses
> `esp32-s3-devkitc-1`, and the env names are unchanged. The MQ-7 has been removed
> from the hardware and replaced by an **SGP30** on the retained 3.3 V supply (D18).

## Pin map (Pin Layout, 22 Oct 2026; architecture §5)

| Signal | GPIO | Notes |
|--------|------|-------|
| I2C SDA / SCL (BME280 0x76, SGP30 0x58) | 21 / 47 | 4.7 kOhm pull-ups to 3.3 V |
| DS18B20 DQ | 14 | 4.7 kOhm pull-up, external 3.3 V supply |
| DFR0026 light | 1 (ADC1_CH0) | 10 kOhm series, 1 uF to GND |
| SEN0244 TDS | 6 (ADC1_CH5) | 100 nF to GND, filtered external 5 V |
| pH-4502C PO | 7 (ADC1_CH6) | 10 kOhm / 15 kOhm divider, 1 uF; DO/TO not connected |
| HC-SR04 TRIG / ECHO | 17 / 18 | ECHO through 1 kOhm / 2 kOhm divider |
| External-power sense | 8 (ADC1_CH7) | 220 Ohm over 2 x 220 Ohm from external 5 V (~3.33 V) |
| Relay drive | 15 | IRLZ44N low side, 220 Ohm gate, 10 kOhm pull-down; active-high |
| Native USB D- / D+ | 19 / 20 | 22 Ohm series |

GPIO35-37 are used by the PSRAM; GPIO0, 3, 45 and 46 are strapping pins.

> `include/board_profile.h` matches this table (remapped 22 Oct 2026).
> `BOARD_POWER_SENSE_VERIFIED` stays `false`, so the build reports non-production
> until GPIO8 is measured on the board. The MQ-7 code is gone: `SENSOR_MQ7` and
> `METRIC_CO_SIGNAL` became `SENSOR_SGP30`, `METRIC_TVOC` and `METRIC_CO2EQ`
> (`Snapshot` 528 -> 584 bytes), and `drivers/mq7.*` became the `drivers/sgp30.*`
> stub with its own `test_sgp30` env.

New layered FreeRTOS firmware. The design authority is
[`Firmware Architecture.pdf`](Firmware%20Architecture.pdf), Rev 3 (22 Oct 2026). Its
editable source is [`docs/architecture/`](docs/architecture/); rebuild with
`python docs/architecture/build.py` (needs ReportLab). Section numbers (§) below
refer to Rev 3. The old team code is kept unchanged and is used only as reference.

**Status: Stage 1 skeleton plus first drivers.** The shared contracts are in
place and every module exists and compiles. The BME280 (WP1) and pH (WP5)
drivers are written; the others are still stubs (see "Work packages").

## Language: C (migrated from C++, October 2026)

All project sources (`src/`, `include/`, `test/`) are C. The module layout, layer
rules, frozen-contract contents and env names are the same as in the C++ version.
Runtime behaviour is meant to be unchanged, but it has not yet been re-verified on
hardware (see "C migration build record" below). The pre-migration C++ tree was
not kept; its build numbers are in that record. The framework is still
Arduino-ESP32 on the same pinned platform, so the SDK settings below still apply.
The framework is C++ inside, but project code only uses its C APIs (`esp32-hal-*`,
FreeRTOS, `esp_timer`).

The architecture records this as decision **D17** (Rev 3, §7 and §20).
`Stage 1 Report.html` still describes the C++ code.

How the C++ constructs map to C:

| C++ | C |
|-----|-----|
| `namespace cfg` / `board` constants | `CFG_*` / `BOARD_*` macros with the original types, e.g. `CFG_BME280_PERIOD_MS`, `BOARD_PIN_I2C_SDA` |
| `enum class X : uint8_t` | `typedef uint8_t X;` plus prefixed constants (`SENSOR_*`, `METRIC_*`, `UNIT_*`, `HEALTH_*`, `DRIVER_*`, `CAPTURE_*`, `POWER_MODE_*`, `TASK_ID_*`, `LOG_LEVEL_*`, ...). Records keep their C++ size. `reading.c` checks `sizeof(Reading) == 56` and `sizeof(Snapshot) == 584` at compile time (528 before the SGP30 replaced the MQ-7). |
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

## Layout (§7)

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
  docs/architecture/        ReportLab source of Firmware Architecture.pdf
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
| WP5 | pH + TDS, calibration | `drivers/ph.*`, `drivers/tds.*`, `domain/calibration.*` | 3 | — (**pH driver + ADC HAL written 17 Oct 2026, builds; not yet run on hardware, uncalibrated** — see "pH-4502C calibration") |
| WP6 | SGP30 air quality (TVOC, CO2eq) on the shared I2C bus | `drivers/sgp30.*`, `test/bringup/sgp30_main.c` | 2 | raw 16-bit command write/read in the I2C HAL |
| WP7 | Acquisition scheduler, Safety, alarm rules, snapshot | `services/acquisition.*`, `services/safety.*`, `domain/alarm.*` | 4 | ≥1 working driver; thresholds |
| WP8 | Wi-Fi/MQTT, BLE, protocol, configuration, desktop app update | `transports/*`, `services/configuration.*` | 5 | WP7 snapshot |
| WP9 | Power ISR, rail epoch, lease handover | `services/power_coordinator.*`, `services/rail_state.*` | 6 | power sense on GPIO8 measured on the board |
| WP10 | Logger queue, supervisor, watchdogs | `diagnostics/*`, `services/supervisor.*` | 4 | — |

WP2, WP3, WP6 and WP10 can start in parallel. A driver counts as done when it meets the "Completion evidence" column of the §16 table. Compiling is not enough.

## Commissioning gates (hardware, §20)

The pins below are now on the drawing, but their behaviour must be measured on
the board before the matching feature is switched on. Do not guess the open items in code.

- External-power sense on GPIO8: actual levels at the real USB voltage (~3.33 V at 5.00 V, ~3.5 V at 5.25 V), decay and bounce. The divider has no hysteresis.
- Per-branch 5 V rail feedback: not on the drawing; use measured settle timing.
- Relay on GPIO15 (active-high, IRLZ44N): which contact (NO/NC) is used, the driven load and its safe state.
- SGP30 module: 3.3 V supply and logic levels, pull-ups, baseline storage policy.
- Light ADC headroom (the DFR0026 can exceed the S3's ~3.1 V range), and the pH range measured on the board (nominally 3.0 V max at the pin)
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

## pH-4502C calibration (WP5)

Wiring: PO -> 10 kOhm -> GPIO7 (ADC1_CH6), 15 kOhm and 1 uF to GND. DO and TO
are not connected. A 5 V module output is 3.0 V at the pin, inside the S3's
~3.1 V range at 11 dB (the ~2.45 V limit in Rev 1-2 was for the original ESP32).

The driver averages a spaced window: 10 conversions 30 ms apart, sorted, mean of
the middle 6 (the tutorial's filter). Until two buffer points are entered it
publishes the module voltage in mV with `HEALTH_UNCALIBRATED` (D13). The
bring-up harness prints those values with `(uncal)`.

```bash
pio run -e test_ph -t upload -t monitor
```

1. **Offset (once per module).** Unplug the probe and short the BNC centre to
   its shell. Turn the pot nearest the BNC until the line reads about
   `2500 mV (uncal)`, which is pH 7. A factory-set module puts pH 7 near 0 V,
   so it first prints `ph=UNPOWERED` (PO below about 170 mV). Check that the
   5 V rail is on, then keep turning the pot until mV values appear.
2. **Buffers.** Plug the probe in. Rinse it, put it in the pH 6.86 buffer, wait
   at least 2 minutes and note the mV. Rinse it again, then do the same with
   pH 4.01. Never move the probe between solutions without rinsing.
3. **Fit.** Enter both mV values as `PH_BUFFER_A_MV` and `PH_BUFFER_B_MV` in
   `test/bringup/ph_main.c` and flash it again. The slope is printed at boot.
   The tutorial's nominal slope is about -0.0057 pH/mV. Readings then show
   `pH` with health OK.
4. **Check.** Measure a third buffer (for example 9.18) that was not used for
   the fit, and record the error and the noise. Recalibrate from time to time,
   because probes drift.

**Without buffers (nominal pH).** While both buffer values are 0, `ph_main.c`
uses a nominal fit and prints pH with `(uncal)`. pH 7 is the module voltage
measured with the BNC shorted (`PH_NOMINAL_MV_AT_PH7`, 2758 mV on 17 Oct 2026),
and the slope is the tutorial's -0.0057 pH/mV. Expect roughly +/-0.5-1 pH, and
measure the shorted value again whenever the offset pot is turned. The driver
reports these readings as `HEALTH_UNCALIBRATED` (`calibrationProvisional`), so
they never count as actionable. Bring-up tests on 17 Oct gave tap water 6.9,
vinegar 3.6 and baking soda 7.8.

Production code does not store the calibration yet. Keeping the fit in NVS is
WP8 (`services/configuration`).

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

**Resolved SDK configuration** (`tools/sdk/esp32/sdkconfig`, checked against §15):

| Setting | Value | Consequence |
|---------|-------|-------------|
| `CONFIG_FREERTOS_HZ` | 1000 | 1 ms tick. `pdMS_TO_TICKS` is exact. |
| `CONFIG_FREERTOS_UNICORE` | not set | Dual core. "Low-power single core" means only application affinity (§15). |
| `CONFIG_PM_ENABLE` | not set | No automatic DFS or light sleep with this framework build (§15). |
| `CONFIG_BTDM_CTRL_PINNED_TO_CORE` | 0 | BT controller on core 0, as §8 assumes. NimBLE host affinity is still to be checked in WP8. |
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
