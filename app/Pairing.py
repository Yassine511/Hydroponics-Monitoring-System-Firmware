"""Chooses the platform's passkey pairing: BlueZ agent (Linux) or WinRT (Windows).

Both take the six-digit passkey the ESP prints on its serial log. An empty
passkey is fine when this computer is already bonded. Other platforms let the
operating system handle pairing on first connection.
"""

import sys


async def pair_controller(device, passkey: str) -> None:
    if sys.platform.startswith("linux"):
        from BluezPairing import pair_controller as pair
    elif sys.platform == "win32":
        from WinPairing import pair_controller as pair
    else:
        return
    await pair(device, passkey)


async def forget_controller(device) -> None:
    """Removes the bond on this computer (the ESP keeps its own until its bond is cleared)."""
    if sys.platform.startswith("linux"):
        from BluezPairing import forget_controller as forget
    elif sys.platform == "win32":
        from WinPairing import forget_controller as forget
    else:
        return
    await forget(device)
