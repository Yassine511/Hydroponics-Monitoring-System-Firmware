// BleTransport: GATT server, bounded RX/TX pools, sole GATT sender.
// Work package WP8 (Stage 5). Architecture §15.
//
// Keeps the UUID map, 2-byte LE length framing and 4096-byte payload limit
// from BluetoothControlApp/FirmwareContract.md. Callbacks copy and enqueue
// only - they never touch sensor objects.
#pragma once
#include <stdbool.h>

bool bleTransportInit(void);
