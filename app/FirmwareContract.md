# ESP32 contract, version 2

Version 2 is implemented by the production firmware (the repository root, one level up from `app/`, `PROTOCOL_FIRMWARE_VERSION` 0.9.0: WP8 and WP9). It replaces [version 1](FirmwareContract-v1.md), which only the simulated [ESP demo](ESP-Demo/BuildGuide.md) implements. Firmware and app change together: any field change here must be mirrored in `src/domain/protocol_codec.*`, `Protocol.py`, `Catalog.py` and `DemoController.py`.

**Bluetooth LE is the only link** between the app and the controller: it carries State, Telemetry, events and commands. The controller's Wi-Fi is a setting (`set_wifi` / `forget_wifi`) kept for future server publishing; the app never connects over it. The read-only LAN link of Stage 8.4 (TCP 47800, mDNS, `hello` token, `get_lan_token`, `rotate_lan_token`) was removed: those operations now get `unknown_op`.

## Framing

Each message is UTF-8 JSON preceded by a two-byte little-endian payload length.

- The maximum payload is 4096 bytes. A length of 0 or more than 4096 is a protocol error: the receiver drops its buffer, and the BLE side disconnects.
- Packets may split or merge frames, so both sides reassemble.
- Every message carries `"v":2`. A receiver rejects any other version.

## Bluetooth LE

| Purpose | UUID | Access |
| --- | --- | --- |
| Hydroponics service | 98a90f00-346e-4e65-9cbd-7687685bdc01 | Advertised |
| State | 98a90f01-346e-4e65-9cbd-7687685bdc01 | Authenticated read and notify |
| Telemetry | 98a90f02-346e-4e65-9cbd-7687685bdc01 | Authenticated notify |
| Command | 98a90f03-346e-4e65-9cbd-7687685bdc01 | Authenticated write with response |
| Result | 98a90f04-346e-4e65-9cbd-7687685bdc01 | Authenticated notify (results and events) |
| Auth gate | 98a90f05-346e-4e65-9cbd-7687685bdc01 | Authenticated read, value `HYDRO_SECURE_V1` |

**Security:**

- The firmware uses LE Secure Connections with bonding and MITM protection, and its IO capability is display-only.
- At boot it generates a six-digit passkey and prints it once on the local serial log. The app asks the user for this passkey and supplies it on every platform: through the BlueZ agent on Linux and through WinRT custom pairing on Windows.
- Only one bonded owner is accepted. Another device is refused while a bond exists.
- **Bond recovery:** hold the BOOT button for 10 s while the firmware is running. Holding it through reset does not work, because GPIO0 is a strap pin and the chip enters download mode instead.

**Traffic:**

- State is read once at connection, then notified every 2 s and immediately after a change.
- The app subscribes to Telemetry for the whole connection, whatever page is open. The ESP notifies it every 3 s.
- Notifications are chunked to the negotiated MTU minus 3.
- The app writes commands in ordered chunks with write-with-response.
- When the link is congested, a superseded telemetry frame is dropped. Results and events are never dropped for telemetry.
- The app treats State as stale after 15 s.

## State

~~~json
{"v":2,"type":"state","node":"Hydro-ESP1","fw":"0.9.0","boot":2864150233,"uptime_ms":734120,
 "link":"ble","mode":"normal","power_epoch":3,"cfg_rev":3,
 "wifi":{"state":"connected","ssid":"Greenhouse","ip":"192.168.1.42","rssi":-61},
 "safety":{"relay":true,"lamp_reason":"level","interlock":false,"latched":[],"active":[],"stale":[]},
 "sgp30_baseline":"learning",
 "enabled":["bme280","ds18b20","light","hcsr04","ph","tds","sgp30"]}
~~~

| Field | Meaning |
| --- | --- |
| `boot` | Random number chosen at each boot. Together with `seq`, it lets the app remove duplicate samples. |
| `link` | Always `"ble"`. |
| `mode` | Lowercase `powerModeName()` from the WP9 power coordinator: `normal` (external 5 V present), `transition_to_low`, `low_power` (external 5 V lost: Wi-Fi off, pH / TDS / HC-SR04 `UNPOWERED`, relay off; BLE and the 3.3 V sensors keep running), `transition_to_normal`, `degraded` (a transition step was not acknowledged in time; the controller keeps waiting and never runs two acquisition owners) or `unknown` (before the coordinator's first decision). With the firmware kill switch `POLICY_POWER_TRANSITIONS_ENABLED 0` it only reports `normal` / `low_power` from the sense pin. There is no battery field: nothing measures the battery. |
| `power_epoch` | The 5 V rail epoch: it changes on every external-power edge. |
| `wifi.state` | `unconfigured`, `connecting`, `connected` or `failed`. `ip` and `rssi` are present only when the state is `connected`. The password is never sent. |
| `safety.*` | Mirrors `SafetyStatus`. `latched`, `active` and `stale` are lists of rule ids. `lamp_reason` is `lampReasonName()`: `no data`, `level`, `dwell`, `fault`, `stale`, `interlock`, `disabled`, `no 5 V` (low power: the relay coil has no supply) or `input missing` (an enabled relay-interlock rule has no fresh input: the relay is held OFF until it has). |
| `sgp30_baseline` | `none`, `learning`, `restored` or `valid`. |
| `enabled` | Sensor keys that are currently enabled (see Sensors). |

## Telemetry

The ESP sends one frame with all ten metrics, keyed by `metricName()`:

~~~json
{"v":2,"type":"telemetry","boot":2864150233,"cfg_rev":3,"m":{
 "air_temperature":{"value":23.41,"unit":"degC","health":"OK","age_ms":120,"seq":2911,"q":0,"cal_rev":0},
 "ph":{"value":6.21,"unit":"pH","health":"OK","age_ms":610,"seq":727,"q":0,"cal_rev":12},
 "tds":{"value":null,"unit":"ppm","health":"STALE","age_ms":9120,"seq":702,"q":0,"cal_rev":1},
 "...":{}}}
~~~

- **`value`** is a number only when `health` is `OK` or `UNCALIBRATED`. Otherwise it is `null`. In particular, a retained last-good value is never sent as a new observation, and `UNPOWERED` never carries a value.
- **`health`** is `healthName()`: `STARTING`, `WARMING`, `OK`, `STALE`, `FAULT`, `TIMEOUT`, `UNPOWERED`, `UNCALIBRATED` or `DEPENDENCY_INVALID`. A disabled sensor's metrics report `null`, and their sensor key is missing from `enabled` in State.
- **`unit`** is `unitName()`. pH reads in `pH` with the compiled nominal fit (health `UNCALIBRATED`) until it is calibrated on the device (health `OK`). Light reads in `mV` (`UNCALIBRATED`) until a lux calibration, then in `lux` (`OK`). TDS is `ppm` throughout, `UNCALIBRATED` until a K-factor is stored.
- **`cal_rev`** is the configuration revision at which the sensor was calibrated on the device, 0 for the factory default.
- **`age_ms`** is the ESP time minus the end of the sample interval. The ESP has no wall clock, so the app computes the sample time as receive time minus `age_ms`.
- **`seq`** is a per-metric counter that restarts at boot. It advances on every observation, failed ones included (`FAULT`, `TIMEOUT`, `UNPOWERED`, ...), so a health change after a good sample is a new row, never a re-send. The app stores each sample once, keyed on (node, boot, metric, seq). Telemetry goes out every 3 s, so stored history has about 3 s resolution, even though most sensors sample every 1 s.
- **5 V sensors** (`hcsr04`, `ph`, `tds`): a reading measured in an older rail epoch, or sent while the rail is down, is sent as `UNPOWERED` with a `null` value, even if acquisition accepted it earlier.
- **`q`** is a bit field of quality flags: 1 saturated, 2 uncompensated, 4 filter partial, 8 late.

## Sensors

Sensors are fixed by the board. They can be enabled or disabled, but not added.

| Key | Sensor | Metrics |
| --- | --- | --- |
| `bme280` | BME280 | `air_temperature`, `air_humidity`, `air_pressure` |
| `ds18b20` | DS18B20 | `water_temperature` |
| `light` | DFR0026 | `light_level` |
| `hcsr04` | HC-SR04 | `water_distance` |
| `ph` | pH-4502C | `ph` |
| `tds` | SEN0244 | `tds` |
| `sgp30` | SGP30 | `tvoc`, `co2eq` |

Disabling some sensors affects others:

- **`bme280`:** the SGP30 loses humidity compensation.
- **`ds18b20`:** TDS loses temperature compensation.
- **`light`:** the automatic lamp stays OFF.

## Commands (BLE only)

~~~json
{"v":2,"id":"9fb8ab0e-7375-4075-b078-bca8456c8014","op":"set_sensor_enabled","data":{"sensor":"tds","enabled":false}}
~~~

The `id` is at most 40 characters. Each command gets exactly one Result frame with the same `id`.

| Operation | `data` | Success `data` |
| --- | --- | --- |
| `get_sensors` | `{}` | `{"sensors":[{"key","label","metrics":[…],"enabled","depends_note"}]}` |
| `set_sensor_enabled` | `{"sensor","enabled"}` | `{"applied":true,"cfg_rev":n}` |
| `reset_latch` | `{"rule":n}` | `{"applied":true}` |
| `set_wifi` | `{"ssid" (1–32 B),"password" (8–63 chars)}` | `{"stored":true}`. Watch `wifi.state` in State for the connection result. |
| `forget_wifi` | `{}` | `{"stored":true}` |
| `get_calibration` | `{}` | `{"calibration":{"ph":{"overridden","rev","valid","slope","offset"},"tds":{…},"light":{…}}}` |
| `cal_capture` | `{"sensor"}` | `{"sensor","x","age_ms"}`: the sensor's raw input now, scaled (below) |
| `set_calibration` | `{"sensor","x1","y1"[,"x2","y2"]}` | `{"applied":true,"cfg_rev":n}` |
| `reset_calibration` | `{"sensor"}` | `{"applied":true,"cfg_rev":n}` |

**Calibration** (Calibration page). Only `ph`, `tds` and `light` take one; any other sensor gets `bad_field`. Numbers are scaled integers (the parser keeps no fractions):

| Sensor | Points | `x` (from `cal_capture`) | `y` (reference) | Fit |
| --- | --- | --- | --- | --- |
| `ph` | 2 buffers, ≥ 1 pH apart | module mV × 10 | pH × 1000 | two-point line, \|slope\| 0.002–0.02 pH/mV |
| `tds` | 1 solution | ppm at 25 °C before K, × 10 | ppm × 10 | K = y / x, 0.5–2.0 |
| `light` | 1 lux-meter reading | pin mV × 10 | lux × 10 | lux per mV through zero, 0.01–100 |

- `cal_capture` reads the latest filtered sample from the snapshot (the controller never touches the sensor bus for it). It fails with `stale` if the sensor is off, unpowered (low power), faulty or has no sample newer than 5 s, or for TDS when the water temperature is missing.
- `set_calibration` fits the points, refuses an implausible fit with `bad_fit`, and refuses with `rule_latched` / `rule_active` while an alarm rule on that sensor is latched / active. The fit is stored in the controller's flash (ConfigBlob schema 2, `calOverridden`), survives reboots and firmware updates (the compiled defaults no longer overwrite it), and applies from the next sample. `reset_calibration` restores the compiled default.
- The grow-lamp thresholds stay in light-sensor pin mV: a lux calibration never changes when the lamp switches.

**Result frames:**

~~~json
{"v":2,"type":"result","id":"9fb8…","status":"ok","data":{"applied":true,"cfg_rev":4}}
{"v":2,"type":"result","id":"9fb8…","status":"error","error":"rule_latched"}
{"v":2,"type":"result","id":"9fb8…","status":"busy"}
~~~

- `ok` is sent only after the change has been persisted and applied, never just because the command was accepted.
- `busy` means the command queue is full. The app may retry.
- Error strings: `bad_request`, `unknown_op`, `bad_field`, `rule_latched` (a sensor cannot be disabled or calibrated while one of its rules is latched), `rule_active` (a latch cannot be reset, or its sensor calibrated, while its condition persists), `expired`, `timeout`, `storage`, `stale` and `bad_fit` (calibration, above).
- Configuration changes never override a latched alarm or an active interlock.

## Events

Alarm events come from a 64-entry journal. They are sent on the Result characteristic.

~~~json
{"v":2,"type":"event","n":118,"rule":0,"level":"CRITICAL","kind":"trip","age_ms":40,"gap":false}
~~~

- `n` increases by one for each event.
- `gap:true` means some older events were lost: the journal overflowed, or an event could not be sent before the previous Bluetooth link closed. An event that fails to send on a live link stays queued and is sent again.
