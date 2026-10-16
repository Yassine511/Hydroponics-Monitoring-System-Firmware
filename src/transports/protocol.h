// Protocol adapters: snapshot -> JSON state/telemetry frames.
// Work package WP8 (Stage 5). Architecture §15.
//
// The ONLY place JSON is built. Never called from acquisition or ISRs.
// Coordinate schema changes with BluetoothControlApp Protocol.py/Catalog.py.
#pragma once
#include <stddef.h>

#define PROTOCOL_FRAME_HEADER_BYTES ((size_t)2)  // little-endian payload length
#define PROTOCOL_MAX_PAYLOAD_BYTES ((size_t)4096)
