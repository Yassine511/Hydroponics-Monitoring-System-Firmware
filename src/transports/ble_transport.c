#include "transports/ble_transport.h"

// TODO(WP8): port the ESP-Demo GATT service. NimBLE-Arduino 2.5.1 (pinned in
// platformio.ini) is a C++ wrapper; from C, use the NimBLE host C API it
// bundles (ble_gatts_*, ble_gap_*).

bool bleTransportInit(void) { return false; }
