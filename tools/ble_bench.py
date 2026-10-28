"""Configuration bench over BLE, driven by the desktop app's own BleLink.

The PC must already be bonded with the ESP (pair once from the app), so no
passkey is needed. Run with the app's virtual environment:

  APP=app
  $APP/.venv/Scripts/python tools/ble_bench.py $APP before
  (reboot the ESP)
  $APP/.venv/Scripts/python tools/ble_bench.py $APP after
  $APP/.venv/Scripts/python tools/ble_bench.py $APP stream 90   # keep a link streaming (radio-load tests)

before: switch TDS off, read the LAN token twice and rotate it, store test
        Wi-Fi credentials, send a bad latch reset, repeat a no-op change.
after:  check that all of it survived the reboot, then restore (TDS on,
        Wi-Fi forgotten).
"""
import asyncio
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, sys.argv[1])
from BleLink import BleLink, scan_ble  # noqa: E402
from Protocol import parse_state, parse_telemetry  # noqa: E402

STATE_FILE = Path(__file__).with_name(".ble_bench_state.json")
latest: dict = {}
counts: dict = {}


def on_message(_link, message):
    kind = message.get("type")
    counts[kind] = counts.get(kind, 0) + 1
    if kind == "state":
        latest["state"] = parse_state(message)
    elif kind == "telemetry":
        latest["telemetry"] = {s["metric"]: s for s in parse_telemetry(message)}
        latest["telemetry_cfg_rev"] = message.get("cfg_rev")
    elif kind == "event":
        print("event:", message)


async def connect() -> BleLink:
    for _ in range(3):
        targets = await scan_ble(6.0)
        if targets:
            link = BleLink(on_message, lambda _l, reason: print("disconnected:", reason))
            await link.connect(targets[0], "")
            await asyncio.sleep(7)  # a State and two Telemetry frames
            return link
    raise SystemExit("controller not found")


def show(label):
    st, tm = latest.get("state", {}), latest.get("telemetry", {})
    tds = tm.get("tds", {})
    print(f"[{label}] fw={st.get('fw')} cfg_rev={st.get('cfg_rev')} telemetry cfg_rev={latest.get('telemetry_cfg_rev')} "
          f"enabled={st.get('enabled')} wifi={st.get('wifi')} sgp30={st.get('sgp30_baseline')} "
          f"tds={{value: {tds.get('value')}, health: {tds.get('health')}}}")


async def call(link, op, data):
    try:
        result = await link.send(op, data)
        print(f"{op} -> ok {result}")
        return result
    except Exception as error:
        print(f"{op} -> {type(error).__name__}: {error}")
        return None


async def before():
    link = await connect()
    show("start")
    await call(link, "set_sensor_enabled", {"sensor": "tds", "enabled": False})
    await asyncio.sleep(7)
    show("tds off")
    t1 = await call(link, "get_lan_token", {})
    t2 = await call(link, "get_lan_token", {})
    t3 = await call(link, "rotate_lan_token", {})
    print("token stable across gets:", bool(t1) and t1 == t2, "| rotate changed it:", bool(t3) and t3 != t1)
    await call(link, "set_wifi", {"ssid": "BenchNet", "password": "bench-pass-123"})
    await call(link, "reset_latch", {"rule": 5})  # bad rule -> error
    await call(link, "set_sensor_enabled", {"sensor": "tds", "enabled": False})  # no change -> ok
    await asyncio.sleep(3)
    show("after commands")
    STATE_FILE.write_text(json.dumps({"cfg_rev": latest["state"]["cfg_rev"], "token": (t3 or {}).get("token")}))
    await link.disconnect()


async def after():
    expected = json.loads(STATE_FILE.read_text())
    link = await connect()
    show("after reboot")
    st = latest["state"]
    t = await call(link, "get_lan_token", {})
    print("persisted: cfg_rev", st["cfg_rev"] == expected["cfg_rev"], "| tds still off", "tds" not in st["enabled"],
          "| wifi ssid", st["wifi"].get("ssid") == "BenchNet", "| token", (t or {}).get("token") == expected["token"])
    await call(link, "set_sensor_enabled", {"sensor": "tds", "enabled": True})
    await call(link, "forget_wifi", {})
    await asyncio.sleep(8)
    show("restored")
    await link.disconnect()


async def stream(seconds: float):
    link = await connect()
    start = time.monotonic()
    while time.monotonic() - start < seconds:
        await call(link, "get_sensors", {})  # extra command traffic on top of State/Telemetry
        await asyncio.sleep(2)
    print(f"frames received in {seconds:.0f} s: {counts}")
    await link.disconnect()


mode = sys.argv[2]
asyncio.run(before() if mode == "before" else after() if mode == "after" else stream(float(sys.argv[3])))
