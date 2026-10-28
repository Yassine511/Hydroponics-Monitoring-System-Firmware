"""LAN link bench (Stage 8.4), using the desktop app's own BleLink and LanLink.

The PC must be bonded with the ESP and the ESP must already be on Wi-Fi
(credentials sent from the app's Wi-Fi page). The app itself must not be
connected over Bluetooth while this runs (one BLE client at a time).

  APP=app
  $APP/.venv/Scripts/python tools/lan_bench.py $APP checks      # token, refusal, read-only, rotate
  $APP/.venv/Scripts/python tools/lan_bench.py $APP stream 90   # keep LAN (and BLE) busy N seconds

checks: get the token and IP over BLE, then over TCP:
  wrong token -> unauthorized and closed; right token -> State/Telemetry flow;
  a command over LAN -> ble_only; rotate the token over BLE -> LAN client dropped,
  old token refused, new token accepted.
"""
import asyncio
import json
import sys
import time
import uuid

sys.path.insert(0, sys.argv[1])
from BleLink import BleLink, scan_ble  # noqa: E402
from Links import Target  # noqa: E402
from LanLink import LanLink  # noqa: E402
from Protocol import LAN_PORT, Decoder, encode, parse_state  # noqa: E402

counts: dict = {}
latest: dict = {}


def on_message(link, message):
    key = f"{link.kind}:{message.get('type')}"
    counts[key] = counts.get(key, 0) + 1
    if message.get("type") == "state":
        latest[link.kind] = parse_state(message)


async def ble_connect() -> BleLink:
    for _ in range(3):
        targets = await scan_ble(6.0)
        if targets:
            link = BleLink(on_message, lambda _l, reason: print("BLE disconnected:", reason))
            await link.connect(targets[0], "")
            await asyncio.sleep(3)
            return link
    raise SystemExit("controller not found over BLE")


async def raw_session(host, token, extra=None, seconds=4.0):
    """Hello with `token`, optionally send one more command; returns (replies, frame types)."""
    reader, writer = await asyncio.wait_for(asyncio.open_connection(host, LAN_PORT), 5)
    hello_id = str(uuid.uuid4())
    writer.write(encode({"v": 2, "id": hello_id, "op": "hello", "data": {"token": token}}))
    if extra:
        writer.write(encode({"v": 2, "id": "extra", "op": extra[0], "data": extra[1]}))
    await writer.drain()
    decoder, replies, types, closed = Decoder(), [], [], False
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            data = await asyncio.wait_for(reader.read(4096), deadline - time.monotonic())
        except asyncio.TimeoutError:
            break
        if not data:
            closed = True
            break
        for m in decoder.feed(data):
            types.append(m.get("type"))
            if m.get("type") == "result":
                replies.append((m.get("id"), m.get("status"), m.get("error")))
    writer.close()
    return replies, types, closed


async def checks():
    ble = await ble_connect()
    state = latest["ble"]
    print("Wi-Fi as seen over BLE:", state["wifi"])
    host = state["wifi"]["ip"]
    if state["wifi"]["state"] != "connected" or not host:
        raise SystemExit("the ESP is not on Wi-Fi: send credentials from the app's Wi-Fi page first")
    token = (await ble.send("get_lan_token", {}))["token"]

    replies, types, closed = await raw_session(host, "0" * 32)
    print("wrong token:", replies, "| closed by ESP:", closed, "<- expect unauthorized + closed")

    replies, types, closed = await raw_session(host, token, ("set_sensor_enabled", {"sensor": "tds", "enabled": False}))
    print("right token + command:", replies, "| frames:", {t: types.count(t) for t in set(types)},
          "<- expect hello ok, extra ble_only, state+telemetry")

    lan = LanLink(on_message, lambda _l, reason: print("LAN disconnected:", reason))
    await lan.connect(Target("lan", "Hydro-ESP1", host, LAN_PORT, node="Hydro-ESP1"), token)
    await asyncio.sleep(7)
    print("LanLink frames in 7 s:", {k: v for k, v in counts.items() if k.startswith("lan")},
          "| LAN State link:", latest.get("lan", {}).get("link"),
          "| same boot both links:", latest.get("lan", {}).get("boot") == latest["ble"]["boot"])

    new = (await ble.send("rotate_lan_token", {}))["token"]
    await asyncio.sleep(2)
    print("after rotate, LanLink still connected:", lan.connected, "<- expect False")
    replies, _, closed = await raw_session(host, token)
    print("old token after rotate:", replies, "closed:", closed, "<- expect unauthorized")
    replies, types, _ = await raw_session(host, new)
    print("new token:", replies[:1], "frames:", {t: types.count(t) for t in set(types)})
    await lan.disconnect()
    await ble.disconnect()


async def stream(seconds):
    ble = await ble_connect()
    state = latest["ble"]
    host, token = state["wifi"]["ip"], (await ble.send("get_lan_token", {}))["token"]
    lan = LanLink(on_message, lambda _l, reason: print("LAN disconnected:", reason))
    await lan.connect(Target("lan", "Hydro-ESP1", host, LAN_PORT, node="Hydro-ESP1"), token)
    start = time.monotonic()
    while time.monotonic() - start < seconds:
        await ble.send("get_sensors", {})  # command traffic on BLE while both links stream
        await asyncio.sleep(2)
    print(f"frames in {seconds:.0f} s:", json.dumps(counts))
    await lan.disconnect()
    await ble.disconnect()


asyncio.run(checks() if sys.argv[2] == "checks" else stream(float(sys.argv[3])))
