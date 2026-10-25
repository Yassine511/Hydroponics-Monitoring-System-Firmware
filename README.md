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
> `BOARD_POWER_SENSE_VERIFIED` is `true` since 25 Oct 2026 (GPIO8 measured with
> `test_power_sense`, see "External-power sense bring-up"); WP9 must apply
> `BOARD_POWER_SENSE_QUALIFY_MS`. The MQ-7 code is gone: `SENSOR_MQ7` and
> `METRIC_CO_SIGNAL` became `SENSOR_SGP30`, `METRIC_TVOC` and `METRIC_CO2EQ`
> (`Snapshot` 528 -> 584 bytes), and `drivers/mq7.*` became the `drivers/sgp30.*`
> stub with its own `test_sgp30` env.

New layered FreeRTOS firmware. The design authority is
[`Firmware Architecture.pdf`](Firmware%20Architecture.pdf), Rev 3 (22 Oct 2026). Its
editable source is [`docs/architecture/`](docs/architecture/); rebuild with
`python docs/architecture/build.py` (needs ReportLab). Section numbers (§) below
refer to Rev 3. The old team code is kept unchanged and is used only as reference.

**Status: Stage 1 skeleton plus first drivers.** The shared contracts are in
place and every module exists and compiles. The BME280 (WP1), DS18B20 (WP2), light (WP3), HC-SR04 (WP4),
pH and TDS (WP5) and SGP30 (WP6) drivers are written; the others are still stubs (see "Work packages").

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
| WP2 | DS18B20 + 1-Wire HAL | `drivers/ds18b20.*`, `hal/onewire_bus.*`, `domain/ds18b20_codec.*` | 2 | — (**driver + bit-banged 1-Wire HAL written 24 Oct 2026, 12-bit, Match ROM, CRC and 85 degC checks; running on S3 hardware 24 Oct 2026: ROM 28-00000098BB42, 762 ms per reading, 0 faults in ~200 readings; reference-thermometer check, unplug and supply-loss tests pending** — see "DS18B20 bring-up") |
| WP3 | DFR0026 light + ADC HAL | `drivers/light.*`, `hal/adc_channel.*` | 2 | — (**driver written 23 Oct 2026, publishes module mV (uncalibrated, no lux); running on S3 hardware 23 Oct 2026: dark 0 mV, room ~65 mV, clips at 3157 mV; unplug behaviour and LED-flicker spread not yet measured** — see "DFR0026 light bring-up") |
| WP4 | HC-SR04 + pulse capture (GPIO edge ISR + `esp_timer`) | `drivers/hcsr04.*`, `hal/pulse_capture.*`, `domain/ultrasonic.*` | 3 | ECHO divider verified (**driver written 24 Oct 2026, nominal 343 m/s, no temperature compensation; not yet run on hardware** — see "HC-SR04 bring-up") |
| WP5 | pH + TDS, calibration | `drivers/ph.*`, `drivers/tds.*`, `domain/calibration.*`, `domain/tds_codec.*` | 3 | — (**pH driver + ADC HAL written 17 Oct 2026, builds; not yet run on hardware, uncalibrated** — see "pH-4502C calibration". **TDS driver written 24 Oct 2026, nominal datasheet curve (uncalibrated); running on S3 hardware 24 Oct 2026: air 0 mV, tap ~1245 mV, hot ~1922 mV, salted water over range (FAULT); compensation checked with the DS18B20 in `test_water` (switched to ppm/EC compensation, ~5 % across 11-34 degC); reference solution not yet tested** — see "SEN0244 TDS bring-up") |
| WP6 | SGP30 air quality (TVOC, CO2eq) on the shared I2C bus | `drivers/sgp30.*`, `domain/sgp30_codec.*`, `hal/i2c_bus.*` (raw write/read), `test/bringup/sgp30_main.c`, `test/bringup/climate_main.c` | 2 | — (**running on S3 hardware 22 Oct 2026: feature set 0x0020 @0x58, 1 s cadence, 13 ms per measurement, no CRC faults; breath and hand-sanitiser spikes seen. Runs with the BME280 on the shared bus in `test_climate`, with humidity compensation. Baseline save/restore, 12 h learning and fault recovery not yet tested**; baseline is RAM-only until WP8 stores it in NVS) |
| WP7 | Acquisition scheduler, Safety, alarm rules, snapshot | `services/acquisition.*`, `services/safety.*`, `domain/alarm.*` | 4 | ≥1 working driver; thresholds |
| WP8 | Wi-Fi/MQTT, BLE, protocol, configuration, desktop app update | `transports/*`, `services/configuration.*` | 5 | WP7 snapshot |
| WP9 | Power ISR, rail epoch, lease handover | `services/power_coordinator.*`, `services/rail_state.*` | 6 | — (power sense on GPIO8 measured 25 Oct 2026; ISR must self-disable + 20 ms qualification) |
| WP10 | Logger queue, supervisor, watchdogs | `diagnostics/*`, `services/supervisor.*` | 4 | — |

WP2, WP3, WP6 and WP10 can start in parallel. A driver counts as done when it meets the "Completion evidence" column of the §16 table. Compiling is not enough.

## Commissioning gates (hardware, §20)

The pins below are now on the drawing, but their behaviour must be measured on
the board before the matching feature is switched on. Do not guess the open items in code.

- External-power sense on GPIO8: **measured 25 Oct 2026** (`test_power_sense`): levels, decay and bounce pass; flag set. Open: HIGH ceiling by calculation only, loaded rail, slow pull. The divider has no hysteresis, so WP9 qualifies edges for 20 ms.
- Per-branch 5 V rail feedback: not on the drawing; use measured settle timing.
- Relay on GPIO15 (active-high, IRLZ44N): which contact (NO/NC) is used, the driven load and its safe state. Breadboard answer (`test_lamp`): grow lamp on NO, de-energised = lamp OFF; PCB still pending.
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

## DS18B20 bring-up (WP2)

Wiring: DQ -> GPIO14 with 4.7 kOhm to 3.3 V, sensor on the retained 3.3 V supply
(external power, not parasite mode). The 1-Wire bus is bit-banged on an
open-drain pin (`hal/onewire_bus.c`), with standard-speed timing from the
datasheet. Each time slot (<= 70 us) runs with interrupts off on that core.
The 480 us reset-low phase runs with interrupts on.

`init()` finds the first device with Search ROM, checks the ROM CRC and the
family code (28h), and from then on addresses only that ROM (Match ROM). If the
EEPROM holds a lower resolution, it writes 12-bit to the scratchpad only, never
to EEPROM, and does this again on every init. Each 1 s frame: `start()` sends
Convert T, the driver waits the full 750 ms, then reads the 9-byte scratchpad
once (~12 ms).

| Situation | Reading |
|-----------|---------|
| Valid scratchpad, -55..+125 degC | value in degC, `HEALTH_OK`, raw = register (1/16 degC) |
| Scratchpad CRC wrong, or config fixed bits wrong (all-zero read) | `HEALTH_FAULT`, no raw |
| Register = 0550h (+85 degC power-on value) | `HEALTH_FAULT`; the next frame re-inits |
| Outside -55..+125 degC | `HEALTH_FAULT` |
| No presence pulse | `init -> NOT_PRESENT` or `poll -> NOT_PRESENT`, re-init next frame |
| `init -> FAULT` repeatedly on a sensor that answers | the scratchpad config byte fails the fixed-bit check; suspect a clone part |

The TDS driver takes this reading through `tdsDriverSetWaterTemperature()`.
`test_water` (see "Water probes together") passes it across in bring-up.
Production acquisition (WP7) still has to do the same.

```bash
pio run -e test_ds18b20 -t upload -t monitor
```

WP2 is done once these are filled in:

| Check | Measured |
|-------|----------|
| ROM code printed at boot | `28-00000098BB42 crc C7` (24 Oct 2026, `test_ds18b20`) |
| Room air vs reference thermometer | 26.00-26.12 degC in room air; no reference thermometer yet |
| Ice water (0 degC) / warm water | tap water ~24.8 degC (still falling slowly); hot water settled at 47.19 degC. Air -> hot water: 24.8 -> 43 degC in ~12 s. Hot -> tap water: 43 -> 27 degC in ~18 s. Ice water not tried yet |
| Line-to-line spread | +/-0.06 degC (one 12-bit step) when settled |
| DQ unplugged, then replugged (recovery) | not measured yet |
| Supply removed, then restored (85 degC check) | not measured yet |
| Start-to-ready time printed by the harness (~760 ms expected) | 762 ms on every reading; no FAULT/NOT_PRESENT in a 3 min run with water changes |

## Water probes together (`test_water`)

`test/bringup/water_main.c` runs the DS18B20 and the TDS driver from one task
with a 1 s frame, like `test_climate` does for the I2C pair. Each frame:

1. It passes the latest DS18B20 reading, from the previous frame, to the TDS
   driver with `tdsDriverSetWaterTemperature()`. A faulty reading is passed
   as is, so the TDS driver reports `NO TEMP` instead of using it. After a
   `NOPRES` frame the last good reading is still used until it is 3 s old.
2. It starts both drivers and polls them in turn. The 750 ms DS18B20
   conversion and the ~560 ms TDS window overlap, so both fit in one frame. Run
   one after the other, they would take ~1.3 s.

TDS is therefore compensated with a water temperature about 1 s old, inside
the 3 s staleness limit. The first frame has no temperature yet, so it shows
`NO TEMP`.

```bash
pio run -e test_water -t upload -t monitor
```

Output is one table row per second, with the header repeated every 20 rows:

```text
        | DS18B20 |  SEN0244 TDS                                |
   time | water T |    raw | uncomp |   @25C | state   | trend    |
      s |    degC |     mV |    ppm |    ppm |         |          |
--------+---------+--------+--------+--------+---------+----------+
     12 |   24.81 |   1245 |    470 |    466 | UNCAL   | stable   |
     55 |   51.94 |   1922 |    825 |    465 | UNCAL   | stable   |
     90 |  NOPRES |   1921 |    824 |      - | NO TEMP | stable   |
```

| Column | Meaning |
|--------|---------|
| water T | DS18B20 in degC, or `FAULT`, `NOPRES` (no presence pulse), `TIMEOUT`, `-` (no result yet) |
| raw | TDS pin mV (filtered window) |
| uncomp | ppm from raw without temperature compensation |
| @25C | ppm compensated to 25 degC, the value production uses; `-` without a fresh water temperature |
| state | `OK`, `UNCAL` (nominal curve, no K-factor), `NO TEMP`, `OVER` (above 1000 ppm or ADC clipped), `FAULT` |
| trend | `stable` once the shown ppm (`@25C`, else `uncomp`) stops drifting: over the last 10 rows, the newer 5 average within 0.4 % (min 2 ppm) of the older 5, and no row is more than 2 % off. Else `settling` |

In a new cup, `raw` and `uncomp` jump within a second, but the DS18B20 takes
~20 s to get most of the way to the water temperature, then creeps for a minute
or more. Until then, `@25C` is off (it can even show a brief false `OVER`). Read
the row once `trend` says `stable`. Same tap water, cold and hot: `uncomp` differs, `@25C` should
match. Events (init, faults, ROM code, compensation on/off, first timings)
print as `# ...` lines between rows. Checks to record:

| Check | Measured |
|-------|----------|
| Compensation switches ON after the first frame | yes: frame 1 `NO TEMP`, frame 2 compensated (24 Oct 2026) |
| Same solution at two temperatures (compensated ppm should match within ~10 %) | tap water at 11.4 degC (1234 mV), 33.6 degC (1768 mV), 11.6 degC (1278 mV): 631 / 620 / 653 ppm after the change to ppm compensation (~5 %). Recomputed from the logged raw mV and temperatures; the first firmware compensated the voltage and gave 683 / 584 / 713 |
| Over range | salted water 2380-2386 mV -> `OVER` at 22-24 degC |
| DS18B20 unplugged: TDS falls back to `NO TEMP` (within ~3 s) | not measured yet |
| No `TIMEOUT (frame guard)` lines over a long run | none in ~4.5 min |
| Re-run with the ppm-compensated firmware and the new trend rule | 24 Oct 2026, read at `stable`: tap water 16.0 degC / 1467 mV -> 688 ppm; warm 42.0 degC / 1957 mV -> 634 ppm (8 % apart; `uncomp` 565 vs 849). Salt: raw pinned at 2378-2386 mV from 42 down to 25 degC, so the module output is clipped (~2.38 V) and the ppm is meaningless; `OVER` throughout once compensated, correct. `stable` took ~30 s (cold), ~55 s (warm), ~45 s (salt) |

## SEN0244 TDS bring-up (WP5)

Wiring: A -> GPIO6 (ADC1_CH5), 100 nF to GND, module on the filtered switched
5 V rail. There is no divider, so pin mV is the module output (0-2.3 V nominal).
The probe is waterproof but the board and connectors are not, and the probe must
stay in water below 55 degC, away from the container wall (datasheet).

The driver averages 15 conversions 40 ms apart (sorted, mean of the middle 5,
~560 ms per window). The datasheet sample takes the median of 30 taps 40 ms apart,
which is ~1.2 s and would not fit the 1 s frame. The conversion is in
`domain/tds_codec.c` and is host-tested:
`ppm = (133.42 V^3 - 255.86 V^2 + 857.39 V) x 0.5 / (1 + 0.02 (T - 25))`.

The temperature compensation divides the **ppm** (conductivity), as DFRobot's
GravityTDS library does. The datasheet's sample sketch divides the **voltage**
before the curve instead. The curve is not linear, so that over-corrects. With
the same tap water at 11.4 / 33.6 / 11.6 degC (`test_water`, 24 Oct 2026):

| Method | @25C ppm (cold / warm / cold) | Spread |
|--------|-------------------------------|--------|
| Voltage compensated (datasheet sketch, first version) | 683 / 584 / 713 | ~20 % |
| ppm compensated (GravityTDS, current) | 631 / 620 / 653 | ~5 % |

`TdsConfig.calibration` is an optional K-factor on that ppm (slope = K, offset 0),
to be fitted against a reference solution. raw is always the filtered pin mV.

| Situation | Reading |
|-----------|---------|
| ADC clipped, or ppm above the rated 1000 ppm | value kept, `kQualitySaturated` + `HEALTH_FAULT` |
| No OK DS18B20 reading younger than 3 s, or outside 0-55 degC | uncompensated value, `kQualityUncompensated` + `HEALTH_DEPENDENCY_INVALID` |
| Compensated, no K-factor | ppm, `HEALTH_UNCALIBRATED` (nominal curve, D13) |
| Compensated, K-factor set | ppm, `HEALTH_OK` |

The 0-1000 ppm range is below many hydroponic nutrient solutions (often
1000-1500 ppm or more). Those readings are FAULT and never actionable. 0 V is
distilled water, a probe in air, an open lead or a dead rail alike, so the
driver cannot report `UNPOWERED`; that is left to acquisition (rail epoch, WP9).

`test_tds` runs the TDS driver alone, without the DS18B20 (use `test_water` for both), so it prints
`tds=DEPENDENCY_INVALID (raw <mV>)` by default. To see ppm, measure the water
with a thermometer and set `TDS_MANUAL_WATER_TEMP_C` in
`test/bringup/tds_main.c`. Bring-up then feeds that value as a fresh DS18B20
reading every frame and prints `<ppm> ppm (uncal)`. This option exists only in
bring-up.

```bash
pio run -e test_tds -t upload -t monitor
```

WP5 (TDS) is done once these are filled in:

| Check | Measured |
|-------|----------|
| Probe in air | 0 mV (24 Oct 2026, `test_tds`) |
| Distilled water | not measured yet |
| Tap water (with water temperature) | 1232-1261 mV (~465 ppm on the nominal curve if at 25 degC; water temperature not measured in this run). Back in the same cup after hot and salt: 1264-1272 mV (+2 %) |
| Reference solution (K-factor = reference / reading) | not measured yet |
| Nutrient solution / over-range sample | salted water 2378-2383 mV -> `tds=FAULT` (above 1000 ppm), as designed. Hot tap water: 1914-1935 mV, uncompensated ~825 ppm (water temperatures not measured in this run; see `test_water` for compensated values) |
| Line-to-line spread | about +/-10 mV settled (cold and hot); a few % drift in the first ~10 s after moving the probe |
| Start-to-ready time printed by the harness | 560 ms on every reading |

## DFR0026 light bring-up (WP3)

Wiring: S -> 10 kOhm -> GPIO1 (ADC1_CH0), 1 uF to GND (tau ~10 ms), module on the
retained 3.3 V supply. There is no divider, so pin mV is the module output.

The driver averages 10 conversions 11 ms apart (sorted, mean of the middle 6,
~100 ms per window). The spacing is not a multiple of 10 ms on purpose: lamps
flicker at 100/120 Hz and the RC only partly filters it, so each tap lands on a
different flicker phase. Every reading is module mV with `HEALTH_UNCALIBRATED`.
The datasheet gives no lux conversion, and `types.h` has no lux unit. Lux needs a
reference-meter mapping plus a reviewed `UNIT_LUX` (D13). A clipped tap (bright
light, ~3050 mV) sets `kQualitySaturated`, and the value is then a lower bound,
not a fault. The handover code's raw `analogRead` counts are not reused.

```bash
pio run -e test_light -t upload -t monitor
```

Record the mV for: covered (dark), room light, phone torch (clip point), the
line-to-line spread under LED/fluorescent light, and what happens when S is
unplugged (the 1 uF holds the last level; the driver cannot detect it). WP3 is
done once these are filled in:

| Condition | Measured (23 Oct 2026, `test_light`, 99 ms per window) |
|-----------|----------|
| Dark (covered) | 0 mV fully covered; ~21 mV (20.5-22) with a hand that lets some light in |
| Room light | ~65 mV in a dim room; ~324 mV (323-325, +/-1 mV) with brighter room lighting |
| Torch / saturation | rises through ~480-1190 mV, clips flat at 3157 mV (above the HAL's 3050 mV flag, so `kQualitySaturated` is set) |
| Spread under LED light | not measured yet |
| S unplugged | not measured yet |

Response time is within one 1 s frame in both directions (dark -> torch ~3 frames
while the torch was brought closer; torch -> covered 0 mV in ~3 frames).

## External-power sense bring-up (`test_power_sense`, WP9 gate)

Goal: measure what decides `BOARD_POWER_SENSE_VERIFIED`. The levels at GPIO8
with USB in and out, how long the pin decays through the undefined band, and how
many edges each plug or unplug gives. The divider has no hysteresis.
`power_sense_main.c` timestamps every GPIO8 edge from a CHANGE interrupt (us)
and samples ADC1_CH7 at 1 kHz. Once the level has been stable for
`BOARD_POWER_SENSE_QUALIFY_MS` (20 ms) it prints a `MODE: NORMAL` /
`MODE: LOW-POWER` banner, as the WP9 power task will decide. When the pin has
settled, three indented lines follow: result (PASS/FAIL), timing and bounce. A
heartbeat repeats the mode every 30 s. `v` shows each edge, `d` prints the
table of all transitions.

The ADC is set up once through the IDF driver. `analogReadMilliVolts()` must not
be used on a pin that also needs its digital input: every call runs
`pinMode(pin, ANALOG)`, which disables the input buffer. The test checks that
both paths still agree and prints a `WARNING` line when they do not (ISR deaf,
GPIO/ADC mismatch); then press `a` for digital-only mode. The ADC clips at ~3050 mV, so the high
level (~3.33 V) needs a multimeter at GPIO8: >= 2.475 V and <= 3.6 V.

Pass criteria, all needed before the flag goes true: multimeter HIGH in
2.475-3.6 V; steady LOW max <= 825 mV; every RISE/FALL settles at the right GPIO
level; the edge count and bounce span per transition are recorded (more than
one edge means the power ISR needs a qualification window); and the decay time is known.

```bash
pio run -e test_power_sense -t upload -t monitor
```

The env opens the monitor with DTR/RTS released, so reopening it after an
unplug does not reset the board and the transition history survives.

Hardware result, 25 Oct 2026 (breadboard; power bank on the + rail, ESP32 on
the PC USB). 6 power losses, 5 power-ons and 1 contact dip, over two runs:

| | Power lost (FALL) | Power on (RISE) |
|---|---|---|
| Settled GPIO level | 0 every time | 1 every time |
| Settled ADC | 0-53 mV (<= 825 mV) | 3157 mV, clipped (>= 2475 mV) |
| Edges / bounce span | 479-928 edges over 5.0-14.0 ms, gaps down to 1 us | 3-16 edges over 0.03-0.2 ms |
| Real switching point (ADC at the flip) | ~1650 mV | ~1930-1990 mV |
| Below VIH -> below VIL | 26-181 ms -> 300-453 ms after departure (band ~272 ms) | above VIH 2-4 ms, clip 14-15 ms |
| First GPIO edge after departure | 59-220 ms (+ rail already ~2.5 V) | < 1 ms |

- The divider without hysteresis chatters through ~1.65 V, giving up to ~930
  interrupts in 14 ms. The WP9 power ISR must disable itself on the first edge
  and let the power task re-read the level after a qualification window
  (>= 20 ms suggested, longer than the 14 ms worst case seen), then re-arm.
- The edge arrives when the 5 V branch is already down to ~2.5 V. 5 V readings
  started in the ~60-220 ms before the edge can be invalid, so the rail epoch
  must also reject readings that finished just before it.
- Unplugging can make contact bounce (an ADC dip with no GPIO edge). The digital
  input ignores it.
- The HIGH ceiling (<= 3.6 V) was not measured (no multimeter, ADC clipped):
  4.75-5.25 V x 440/660 = 3.17-3.50 V by calculation.
- The ADC and the GPIO input coexist on GPIO8 (`isr ok`, `mismatch 0`;
  1 `adc-disturbed` in the chatter of one transition).
- Not covered: the + rail under sensor load, a slow cable pull, the power bank's own button.

## Lamp relay bring-up (`test_lamp`, ahead of WP7)

Goal: lamp ON when ambient light is low, OFF when it is high. Breadboard only.
The PCB layout (IRLZ44N stage on GPIO15) is decided after this works, and
`board_profile.h` is unchanged.

Wiring: a TONGLING JQC-3FF-S-Z 5 V "Low Level Trigger" module. VCC to 5 V, GND
to ESP32 GND. IN through a transistor stage like the PCB: GPIO15 -> 220 Ohm ->
N-MOSFET gate (or 1 kOhm -> NPN base), 10 kOhm gate/base to GND, source/emitter
to GND, drain/collector to IN. GPIO15 HIGH = relay ON, so `lamp_main.c` drives
it active-high (`LAMP_RELAY_ACTIVE_LOW 0`), like `BOARD_RELAY1_ACTIVE_LOW`.
Light sensor as in `test_light`, on 3.3 V (on 5 V it can exceed GPIO1's range).
Mains: live (brown) to COM -> NO -> lamp, neutral (blue) straight to the lamp,
NC empty. Relay off (reset, no USB, battery) = lamp OFF.

Hardware result, 24 Oct 2026 (IN straight to GPIO15, no transistor):

- Module VCC not on a live 5 V read ~2.5 V: GPIO15 back-fed it through IN. Check
  VCC is about 5 V and the red PWR LED is on before connecting IN.
- With VCC fixed, covering the sensor (0-4 mV) switched the relay ON after the
  30 s dwell, but GPIO15 HIGH (3.3 V) never switched it OFF. The module's input
  is referenced to its 5 V VCC. Direct drive is ruled out for this module.
- Not yet run with the transistor stage: uncover -> OFF and reset -> OFF.

Test with a low-voltage load on COM/NO first. The 230 V hookup is done with
the supply unplugged, the module in an enclosure away from the ESP32, earth
on the fixture if it is metal (the feed cable is 2-core brown/blue), and
checked by a supervisor before power-on.

```bash
pio run -e test_lamp -t upload -t monitor
```

Decision: `domain/lamp_control` (host-tested). ON below `LAMP_ON_BELOW_MV`, OFF
above `LAMP_OFF_ABOVE_MV`, unchanged in between; each state is held at least
`LAMP_MIN_ON_MS` / `LAMP_MIN_OFF_MS` (boot counts as a switch to OFF). Saturated
counts as bright; `LAMP_FAULTS_TO_OFF` bad readings in a row switch OFF at once.
The 40 / 120 mV, 30 s values are bench placeholders.
Deliberate bring-up deviation from §10: it acts on uncalibrated mV and does not
check reading age, so a stalled sensor freezes the last state (possibly ON).
WP7 must calibrate light or accept relative mV explicitly, and add staleness -> OFF.

Checks: cover the sensor -> ON after the dwell; torch -> OFF after the dwell;
no chatter inside the band; reset or unplug -> OFF and stays OFF through boot.

Thresholds (not yet measured): the sensor may see the lamp it controls, which
would switch it off, then on, in a loop. At the final sensor position record
night lamp OFF, night lamp ON and daylight lamp OFF, and put
`LAMP_OFF_ABOVE_MV` above "night lamp ON". If that is above daylight, shield
the sensor from the lamp or point it at the window.

## HC-SR04 bring-up (WP4)

Wiring: module on the switched external 5 V. TRIG <- GPIO17 (3.3 V logic, inside
the module's TTL input range). ECHO -> 1 kOhm -> GPIO18, 2 kOhm to GND, so 5 V
at the module is ~3.33 V at the pin. **Check the divider before connecting
GPIO18:** with the module's ECHO wire disconnected, put 5 V on the module side
of the 1 kOhm and measure about 3.3 V at the junction (or scope the junction on
a running module). Never force the module's ECHO output to 5 V.

The ECHO pulse is timed by a CHANGE interrupt that stores `esp_timer`
microsecond timestamps (`hal/pulse_capture.c`). `pulseIn()` is not used. One
ping per 1 s frame, 10 us trigger, 30 ms timeout (`CFG_HCSR04_TIMEOUT_MS`).
Distance = width x 343 m/s / 2 (58.3 us/cm), with no air-temperature
compensation (~0.17 %/degC), so every reading carries `kQualityUncompensated`.
A missing distance is never 0:

| Situation | Reading |
|-----------|---------|
| Echo 20-4000 mm | value in mm, `HEALTH_OK` |
| Echo outside 2-400 cm | `HEALTH_FAULT`, raw = width in us |
| ECHO rose but did not fall within 30 ms (nothing in range) | `HEALTH_TIMEOUT`, raw = elapsed us |
| ECHO never rose (stuck low, unplugged, 5 V rail off) | `HEALTH_TIMEOUT`, no raw |
| ECHO high before the trigger (stuck high) | `init`/`start -> FAULT` |

There is no rail-feedback pin, so "rail off" cannot be told apart from
"stuck low". `UNPOWERED` is left to acquisition (rail epoch, WP9). Do not test
"rail off" with `test_hcsr04` running: the harness keeps pulsing TRIG high into
the unpowered module (§12).

```bash
pio run -e test_hcsr04 -t upload -t monitor
```

WP4 is done once these are filled in:

| Check | Measured |
|-------|----------|
| GPIO18 level with ECHO high (divider) | not measured yet |
| Flat target at 10 / 50 / 100 cm (tape vs reading, spread) | not measured yet |
| Open space (> 4 m) | not measured yet |
| ECHO unplugged at the module side (not at GPIO18: no pull, it floats) | not measured yet |
| ECHO held high | not measured yet |
| Target < 2 cm | not measured yet |
| Start-to-ready time printed by the harness | not measured yet |
| Mounting height above the tank, empty/full mapping | not measured yet |

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
