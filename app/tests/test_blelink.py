"""BleLink after a link loss (hardware finding with env:ble_tear, 28 Oct 2026):
a lost client is closed (on Windows its GATT session otherwise keeps the bonded
link up), the decoders forget a cut frame, and an old session's notifications
are never mixed into the current stream."""
import asyncio

from BleLink import BleLink
from Protocol import STATE_UUID, TELEMETRY_UUID, encode


class FakeClient:
    def __init__(self):
        self.closed = 0

    async def disconnect(self):
        self.closed += 1


def frame(message: dict) -> bytes:
    return encode({"v": 2, **message})


def test_lost_link_closes_client_and_clears_cut_frame():
    async def run():
        messages, reasons = [], []
        link = BleLink(lambda _l, m: messages.append(m), lambda _l, r: reasons.append(r))
        client = FakeClient()
        link.client = link.listening = client
        link.connected = True
        whole = frame({"type": "telemetry", "m": {}})
        link._notification(client, TELEMETRY_UUID, whole[:5])  # first chunk only, then the link drops
        link._disconnected(client)
        await asyncio.sleep(0)
        assert client.closed == 1 and reasons == ["Bluetooth connection lost"]
        assert not link.connected and link.client is None
        assert all(len(d.buffer) == 0 for d in link.decoders.values())

        fresh = FakeClient()
        link.client = link.listening = fresh
        link.connected = True
        link._notification(fresh, TELEMETRY_UUID, whole)  # the next frame decodes on its own
        assert messages == [{"v": 2, "type": "telemetry", "m": {}}]

    asyncio.run(run())


def test_old_session_notifications_and_callbacks_are_ignored():
    async def run():
        messages, reasons = [], []
        link = BleLink(lambda _l, m: messages.append(m), lambda _l, r: reasons.append(r))
        old, current = FakeClient(), FakeClient()
        link.client = link.listening = current
        link.connected = True
        link._notification(old, STATE_UUID, frame({"type": "state"})[:4])  # stale half frame
        link._disconnected(old)  # late callback from the old client
        assert link.connected and reasons == [] and messages == []
        link._notification(current, STATE_UUID, frame({"type": "state"}))
        assert messages == [{"v": 2, "type": "state"}]

    asyncio.run(run())


def test_disconnect_closes_even_a_client_that_is_no_longer_connected():
    async def run():
        link = BleLink(lambda *_: None, lambda *_: None)
        client = FakeClient()
        link.client = client
        await link.disconnect()
        assert client.closed == 1

    asyncio.run(run())
