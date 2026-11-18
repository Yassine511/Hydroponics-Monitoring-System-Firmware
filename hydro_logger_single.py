#!/usr/bin/env python3
"""
hydro_logger_single.py — BLE telemetry → single SQLite table.
Adapted for firmware v0.11.0+ (open BLE link, no pairing required).

All metrics are stored in one table called 'readings', with a 'metric' column
to distinguish them. Simple to query across sensors at the same timestamp.

Protocol: v2 frames — {"v":2,"type":"telemetry","boot":…,"cfg_rev":…,"m":{…}}
Frame format: [uint16_LE payload_length][UTF-8 JSON] split into MTU chunks.
Telemetry UUID: 98a90f02-346e-4e65-9cbd-7687685bdc01

Usage:
    python hydro_logger_single.py                          # scan by name
    python hydro_logger_single.py --address AA:BB:CC:DD:EE:FF
    python hydro_logger_single.py --scan                   # list BLE devices

Note: if you previously paired with an older firmware version, remove the
      Hydro-ESP1 pairing from Windows Bluetooth settings before connecting.

Requirements:
    pip install bleak --break-system-packages
"""

import argparse
import asyncio
import json
import logging
import os
import sqlite3
import struct
import sys
from datetime import datetime, timezone

from bleak import BleakClient, BleakScanner

# ── Constants ─────────────────────────────────────────────────────────────────
DEFAULT_DEVICE_NAME = "Hydro-ESP1"
STATE_UUID          = "98a90f01-346e-4e65-9cbd-7687685bdc01"
TELEMETRY_UUID      = "98a90f02-346e-4e65-9cbd-7687685bdc01"
RESULT_UUID         = "98a90f04-346e-4e65-9cbd-7687685bdc01"
PROTOCOL_VERSION    = 2
RECONNECT_DELAY_S   = 5
SCAN_TIMEOUT_S      = 10.0

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DB = os.path.join(SCRIPT_DIR, "hydro_single.db")


# ── Frame reassembler ─────────────────────────────────────────────────────────
class FrameAssembler:
    """
    Firmware frame format: [uint16_LE payload_length][JSON bytes]
    BLE splits frames into MTU-sized (~244 B) notification chunks.
    """
    HEADER_LEN  = 2
    MAX_PAYLOAD = 4096

    def __init__(self):
        self._buf = bytearray()

    def reset(self):
        self._buf.clear()

    def feed(self, chunk: bytes):
        self._buf.extend(chunk)
        while len(self._buf) >= self.HEADER_LEN:
            (payload_len,) = struct.unpack_from("<H", self._buf, 0)
            if payload_len == 0 or payload_len > self.MAX_PAYLOAD:
                logging.warning("Bad frame length %d — buffer reset", payload_len)
                self._buf.clear()
                return
            total = self.HEADER_LEN + payload_len
            if len(self._buf) < total:
                break
            yield bytes(self._buf[self.HEADER_LEN:total])
            del self._buf[:total]


# ── SQLite — single table ─────────────────────────────────────────────────────
# Health values (firmware healthName()): STARTING, WARMING, OK, STALE, FAULT,
#   TIMEOUT, UNPOWERED, UNCALIBRATED, DEPENDENCY_INVALID
# Unit values (firmware unitName()): degC, %RH, Pa, mV, mm, pH, ppm, ppb, lux
SCHEMA = """
CREATE TABLE IF NOT EXISTS readings (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    received_at TEXT    NOT NULL,   -- UTC ISO-8601
    boot_id     INTEGER NOT NULL,   -- random per ESP32 reboot
    cfg_rev     INTEGER NOT NULL,
    metric      TEXT    NOT NULL,   -- "air_temperature", "ph", "tds", …
    value       REAL,               -- NULL when health != OK / UNCALIBRATED
    unit        TEXT    NOT NULL,   -- "degC", "%RH", "Pa", "mV", "mm", "pH", "ppm", "ppb", "lux"
    health      TEXT    NOT NULL,   -- "OK", "STALE", "FAULT", "STARTING", …
    age_ms      INTEGER,            -- ms since measurement; NULL if never measured
    seq         INTEGER NOT NULL,   -- per-metric sample counter on ESP32
    quality     INTEGER NOT NULL,   -- quality flag bits: 1=saturated 2=uncompensated 4=filter_partial 8=late
    cal_rev     INTEGER NOT NULL    -- calibration revision used
);
CREATE INDEX IF NOT EXISTS idx_metric_time ON readings (metric, received_at);
"""

def open_db(path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(path, check_same_thread=False)
    conn.executescript(SCHEMA)
    conn.commit()
    logging.info("Database: %s", path)
    return conn

def store_snapshot(conn: sqlite3.Connection, msg: dict) -> int:
    boot    = msg.get("boot", 0)
    cfg_rev = msg.get("cfg_rev", 0)
    now     = datetime.now(timezone.utc).isoformat()
    rows = [
        (now, boot, cfg_rev, name,
         m.get("value"), m.get("unit", ""), m.get("health", ""),
         m.get("age_ms"), m.get("seq", 0), m.get("q", 0), m.get("cal_rev", 0))
        for name, m in msg.get("m", {}).items()
    ]
    conn.executemany(
        "INSERT INTO readings "
        "(received_at,boot_id,cfg_rev,metric,value,unit,health,age_ms,seq,quality,cal_rev) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?)",
        rows,
    )
    conn.commit()
    return len(rows)


# ── BLE helpers ───────────────────────────────────────────────────────────────
async def scan_all():
    print(f"Scanning for {SCAN_TIMEOUT_S:.0f}s …\n")
    devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT_S)
    if not devices:
        print("No BLE devices found. Check that Bluetooth is enabled.")
        return
    print(f"{'Address':<20}  Name")
    print("-" * 50)
    for d in sorted(devices, key=lambda x: x.name or ""):
        print(f"{d.address:<20}  {d.name or '(no name)'}")
    print(f"\n{len(devices)} device(s) found.")


async def find_by_name(name: str):
    lower = name.lower()
    while True:
        logging.info("Scanning for '%s' …", name)
        devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT_S)
        for d in devices:
            if (d.name or "").lower() == lower:
                logging.info("Found: %s  [%s]", d.name, d.address)
                return d
        logging.warning("Device not found. Use --address if scan keeps missing it.")
        await asyncio.sleep(RECONNECT_DELAY_S)


async def run_session(target, conn: sqlite3.Connection, assembler: FrameAssembler):
    async with BleakClient(target) as client:
        if not client.is_connected:
            logging.warning("Connection failed")
            return

        address = target if isinstance(target, str) else target.address
        logging.info("Connected to %s", address)
        assembler.reset()
        frame_count = 0

        def on_notify(_, data: bytearray):
            nonlocal frame_count
            for payload_bytes in assembler.feed(bytes(data)):
                try:
                    msg = json.loads(payload_bytes)
                except json.JSONDecodeError as exc:
                    logging.warning("JSON error: %s", exc)
                    continue
                # Accept only v2 telemetry frames
                if msg.get("v") != PROTOCOL_VERSION or msg.get("type") != "telemetry":
                    continue
                store_snapshot(conn, msg)
                frame_count += 1
                if frame_count % 20 == 0:
                    logging.info("Stored %d telemetry snapshots", frame_count)

        # The firmware only sends data after bleGattReady(), which requires
        # the client to be subscribed to ALL three notify channels.
        # We process only Telemetry; State and Result subscriptions are
        # required to satisfy the firmware's ready check.
        await client.start_notify(STATE_UUID,     lambda *_: None)
        await client.start_notify(TELEMETRY_UUID, on_notify)
        await client.start_notify(RESULT_UUID,    lambda *_: None)
        logging.info("Subscribed to all channels — collecting …")

        while client.is_connected:
            await asyncio.sleep(1)

        logging.info("Disconnected after %d snapshots", frame_count)


# ── Main ──────────────────────────────────────────────────────────────────────
async def main(db_path: str, device_name: str, address: str | None):
    conn = open_db(db_path)
    assembler = FrameAssembler()

    while True:
        try:
            if address:
                logging.info("Connecting directly to %s …", address)
                target = address
            else:
                target = await find_by_name(device_name)
            await run_session(target, conn, assembler)
        except asyncio.CancelledError:
            break
        except Exception as exc:
            logging.warning("Session error: %s — retrying in %ds", exc, RECONNECT_DELAY_S)
        await asyncio.sleep(RECONNECT_DELAY_S)

    conn.close()
    logging.info("Logger stopped.")


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s  %(levelname)-8s  %(message)s",
        datefmt="%H:%M:%S",
        handlers=[logging.StreamHandler(sys.stdout)],
    )
    parser = argparse.ArgumentParser(description="Hydroponics BLE → SQLite (single table)")
    parser.add_argument("--db",      default=DEFAULT_DB,
                        help="SQLite file (default: next to this script)")
    parser.add_argument("--device",  default=DEFAULT_DEVICE_NAME,
                        help="BLE device name (default: Hydro-ESP1)")
    parser.add_argument("--address", default=None,
                        help="BLE MAC address for direct connect, e.g. AA:BB:CC:DD:EE:FF")
    parser.add_argument("--scan",    action="store_true",
                        help="List all visible BLE devices and exit")
    args = parser.parse_args()

    try:
        if args.scan:
            asyncio.run(scan_all())
        else:
            asyncio.run(main(args.db, args.device, args.address))
    except KeyboardInterrupt:
        print("\nStopped.")
