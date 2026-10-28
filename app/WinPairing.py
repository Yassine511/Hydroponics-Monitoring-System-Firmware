"""Pair the ESP on Windows with the passkey typed in the app.

bleak 3.0.2's WinRT pair() only offers CONFIRM_ONLY ("Just Works"), which the
firmware refuses: it requires MITM protection with a passkey it displays. This
module runs WinRT custom pairing with PROVIDE_PIN and answers the request with
the passkey from the app, so no Windows Settings dialog is needed. The bond is
then held by Windows and reused on later connections.
"""

import asyncio

from winrt.windows.devices.bluetooth import BluetoothLEDevice
from winrt.windows.devices.enumeration import (
    DeviceInformation, DevicePairingKinds, DevicePairingProtectionLevel, DevicePairingResultStatus,
    DeviceUnpairingResultStatus,
)

PAIRING_TIMEOUT = 60.0


def _address(device) -> int:
    text = getattr(device, "address", device)
    return int(str(text).replace(":", "").replace("-", ""), 16)


async def _device_information(device) -> DeviceInformation:
    le = await BluetoothLEDevice.from_bluetooth_address_async(_address(device))
    if le is None:
        raise RuntimeError("Windows cannot find this controller. Scan again with the ESP nearby.")
    try:
        # A fresh object: the one cached on the device is not updated after pairing.
        return await DeviceInformation.create_from_id_async(le.device_information.id)
    finally:
        le.close()


async def pair_controller(device, passkey: str) -> None:
    info = await _device_information(device)
    if info.pairing.is_paired:
        return
    if len(passkey) != 6 or not passkey.isascii() or not passkey.isdigit():
        raise ValueError("Enter the six-digit Bluetooth passkey printed on the ESP serial monitor")
    if not info.pairing.can_pair:
        raise RuntimeError("Windows reports that this controller cannot be paired right now")

    custom = info.pairing.custom
    unexpected: list[str] = []

    def requested(_sender, args):
        if args.pairing_kind == DevicePairingKinds.PROVIDE_PIN:
            args.accept_with_pin(passkey)
        else:
            unexpected.append(args.pairing_kind.name)  # not accepted: Windows cancels the pairing

    token = custom.add_pairing_requested(requested)
    try:
        result = await asyncio.wait_for(
            custom.pair_with_protection_level_async(
                DevicePairingKinds.PROVIDE_PIN, DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION),
            PAIRING_TIMEOUT)
    finally:
        custom.remove_pairing_requested(token)

    if result.status in (DevicePairingResultStatus.PAIRED, DevicePairingResultStatus.ALREADY_PAIRED):
        return
    if unexpected:
        raise RuntimeError(f"The controller asked for an unsupported pairing method ({', '.join(unexpected)})")
    if result.status in (DevicePairingResultStatus.AUTHENTICATION_FAILURE,
                         DevicePairingResultStatus.FAILED):
        raise ValueError("Pairing failed. Check the passkey on the ESP serial monitor and try again.")
    if result.status == DevicePairingResultStatus.REJECTED_BY_HANDLER:
        raise ValueError("Pairing was cancelled")
    raise RuntimeError(f"Windows could not pair with the controller ({result.status.name})")


async def forget_controller(device) -> None:
    info = await _device_information(device)
    if not info.pairing.is_paired:
        return
    result = await info.pairing.unpair_async()
    if result.status not in (DeviceUnpairingResultStatus.UNPAIRED, DeviceUnpairingResultStatus.ALREADY_UNPAIRED):
        raise RuntimeError(f"Windows could not remove the pairing ({result.status.name})")
