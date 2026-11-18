# Hydroponics BLE Data Logger

Two Python scripts that connect to the ESP32 (`Hydro-ESP1`) over Bluetooth, receive sensor telemetry, and store it in a local SQLite database. Both scripts reconnect automatically if the device disconnects.

**Firmware version:** adapted for v0.11.0+ (open BLE link — no pairing or passkey required).

---

## Requirements

Install the only dependency once:

```bash
pip install bleak --break-system-packages
```

---

## Choosing a script

| Script | Database file | Best for |
|---|---|---|
| `hydro_logger_single.py` | `hydro_single.db` | Querying multiple sensors at the same timestamp |
| `hydro_logger_multi.py` | `hydro_multi.db` | Querying one sensor over time (faster, cleaner) |

Both scripts record exactly the same data — only the database structure differs.

---

## Running

**Connect directly by MAC address** (most reliable on Windows):

```bash
python hydro_logger_single.py --address E0:72:A1:F5:84:AD
# or
python hydro_logger_multi.py --address E0:72:A1:F5:84:AD
```

**Connect by device name** (scans for Hydro-ESP1):

```bash
python hydro_logger_single.py
python hydro_logger_multi.py
```

**List all visible BLE devices** (useful to find the MAC address):

```bash
python hydro_logger_single.py --scan
```

Stop either script at any time with **Ctrl+C**.

---

## Connecting for the first time (firmware v0.11.0+)

The new firmware uses an **open BLE link** — no pairing, no passkey, no bonding. Any computer in range can connect directly. Just run the script and it connects.

**If you used an older firmware version** that required a passkey, Windows may still hold an old bond. Remove it first:

1. Open **Settings → Bluetooth & devices**
2. Find `Hydro-ESP1`, click the three dots → **Remove device**
3. Run the logger — it will connect without any pairing prompt

---

## Database structure

### hydro_logger_single.py → `hydro_single.db`

One table called `readings`. Every metric from every snapshot is a separate row.

```
readings
├── id           integer  (auto)
├── received_at  text     UTC timestamp, e.g. "2026-11-11T10:32:01.123456+00:00"
├── boot_id      integer  Random ID assigned at each ESP32 reboot
├── cfg_rev      integer  Configuration revision on the ESP32
├── metric       text     Sensor metric name (see table below)
├── value        real     Physical value; NULL when health is not OK or UNCALIBRATED
├── unit         text     "degC", "%RH", "Pa", "mV", "mm", "pH", "ppm", "ppb", "lux"
├── health       text     "OK", "STALE", "FAULT", "STARTING", "WARMING",
│                         "TIMEOUT", "UNPOWERED", "UNCALIBRATED", "DEPENDENCY_INVALID"
├── age_ms       integer  Milliseconds between measurement and when ESP sent the frame
├── seq          integer  Per-metric sample counter on the ESP32 (restarts at reboot)
├── quality      integer  Quality flag bits: 1=saturated 2=uncompensated 4=filter_partial 8=late
└── cal_rev      integer  Calibration revision used for this reading
```

### hydro_logger_multi.py → `hydro_multi.db`

One table per metric. Same columns as above **minus** `metric` (the table name is the metric). Unknown or future metrics go into a fallback `readings` table that keeps the `metric` column.

```
air_temperature   air_humidity   air_pressure
water_temperature light_level    water_distance
ph                tds            tvoc    co2eq
```

---

## Metric reference

| Metric | Sensor | Normal unit | Notes |
|---|---|---|---|
| `air_temperature` | BME280 | `degC` | |
| `air_humidity` | BME280 | `%RH` | Used by SGP30 for humidity compensation |
| `air_pressure` | BME280 | `Pa` | |
| `water_temperature` | DS18B20 | `degC` | Used by TDS for temperature compensation |
| `light_level` | DFR0026 | `mV` / `lux` | `mV` until on-device calibration, then `lux` |
| `water_distance` | HC-SR04 | `mm` | 5 V rail required; `UNPOWERED` in low-power mode |
| `ph` | pH-4502C | `pH` | 5 V rail required; `UNCALIBRATED` until calibrated |
| `tds` | SEN0244 | `ppm` | 5 V rail required; needs `water_temperature` |
| `tvoc` | SGP30 | `ppb` | `WARMING` for ~12 min on first boot |
| `co2eq` | SGP30 | `ppm` | CO2 estimate, not a direct measurement |

**Health meanings:**

| Health | Meaning |
|---|---|
| `OK` | Fresh, valid, calibrated reading |
| `UNCALIBRATED` | Valid reading but physical units not trustworthy (no calibration stored) |
| `STALE` | Last good value exceeded its age limit |
| `WARMING` | Sensor settling (SGP30 heater, first conversion) |
| `STARTING` | No valid result yet since boot |
| `FAULT` | Transport, CRC, range or conversion error |
| `TIMEOUT` | Operation did not complete in its time budget |
| `UNPOWERED` | 5 V rail off (low-power mode) |
| `DEPENDENCY_INVALID` | Required input missing (e.g. TDS without water temperature) |

---

## Querying the database

Use Python directly (no extra tools needed).

**Last 10 pH readings (single-table):**

```bash
python -c "
import sqlite3
conn = sqlite3.connect('hydro_single.db')
rows = conn.execute('''
    SELECT received_at, value, unit, health
    FROM readings
    WHERE metric = 'ph'
    ORDER BY id DESC LIMIT 10
''').fetchall()
for r in rows: print(r)
conn.close()
"
```

**Last 10 pH readings (multi-table):**

```bash
python -c "
import sqlite3
conn = sqlite3.connect('hydro_multi.db')
rows = conn.execute(
    'SELECT received_at, value, unit, health FROM ph ORDER BY id DESC LIMIT 10'
).fetchall()
for r in rows: print(r)
conn.close()
"
```

**All sensors at the most recent snapshot (single-table):**

```bash
python -c "
import sqlite3
conn = sqlite3.connect('hydro_single.db')
rows = conn.execute('''
    SELECT metric, value, unit, health
    FROM readings
    WHERE received_at = (SELECT MAX(received_at) FROM readings)
    ORDER BY metric
''').fetchall()
for r in rows: print(r)
conn.close()
"
```

**Count of rows per metric (single-table):**

```bash
python -c "
import sqlite3
conn = sqlite3.connect('hydro_single.db')
rows = conn.execute(
    'SELECT metric, COUNT(*) FROM readings GROUP BY metric ORDER BY metric'
).fetchall()
for r in rows: print(r)
conn.close()
"
```

**Only valid readings (health OK):**

```bash
python -c "
import sqlite3
conn = sqlite3.connect('hydro_single.db')
rows = conn.execute('''
    SELECT received_at, metric, value, unit
    FROM readings
    WHERE health = 'OK'
    ORDER BY id DESC LIMIT 20
''').fetchall()
for r in rows: print(r)
conn.close()
"
```

**Tip:** Install [DB Browser for SQLite](https://sqlitebrowser.org) to explore the database visually — open the `.db` file, browse tables, filter rows, and plot trends without writing any queries.

---

## Command-line options

```
--address  AA:BB:CC:DD:EE:FF   BLE MAC address for direct connect
--device   Hydro-ESP1           BLE device name to scan for (default)
--db       /path/to/file.db     Custom database path (default: next to the script)
--scan                          List all visible BLE devices and exit
```

---

## What changed from firmware v0.10 to v0.11

If you used the old logger scripts with the previous firmware, note these differences:

| Item | Old (v0.10) | New (v0.11+) |
|---|---|---|
| BLE pairing | Required 6-digit passkey | None — open link |
| Unit strings | `deg_c`, `percent_rh`, `pascal`, `millivolt`, `millimetre` | `degC`, `%RH`, `Pa`, `mV`, `mm` |
| Health strings | lowercase (`ok`, `stale`) | uppercase (`OK`, `STALE`) |
| Protocol version | No `v` field | `"v":2` in every frame |
| Auth characteristic | Present (…0f05) | Removed |
