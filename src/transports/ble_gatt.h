// GATT server shim over NimBLE-Arduino 2.5.1 (C++), callable from C.
// Work package WP8 (Stage 5). Architecture §17; FirmwareContract.md v2.
//
// The shim is the only code that touches NimBLE. It owns the service map,
// LE Secure Connections pairing with a displayed passkey (MITM), the
// single-owner rule and the link "session generation". It does no protocol
// work: received Command bytes go to one callback, and the caller notifies
// already-framed bytes one chunk at a time.
//
// Threading: the RX callback runs in the NimBLE host task (core 0). It must
// only copy the bytes and return (e.g. xStreamBufferSend with zero wait).
// All other functions may be called from one application task
// (BleTransport). Notifications are sent only on a secure link.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t BleChannel;
enum { BLE_CHANNEL_STATE = 0, BLE_CHANNEL_TELEMETRY, BLE_CHANNEL_RESULT, BLE_CHANNEL_COUNT };

// Command characteristic write, raw bytes as received (any chunking).
typedef void (*BleRxFn)(const uint8_t* data, size_t len, uint32_t generation, void* ctx);

typedef struct BleGattStats {
  uint32_t connects;
  uint32_t disconnects;
  uint32_t pairingsAccepted;
  uint32_t pairingsRejected;   // not authenticated/bonded, or a second owner
  uint32_t notifyFailures;     // chunks NimBLE could not queue
  uint32_t rxDropped;          // writes received while the link was not secure
  int lastDisconnectReason;
} BleGattStats;

// Starts NimBLE, the GATT service and advertising. The six-digit passkey is
// drawn after NimBLE has started the radio (esp_random() is then a true RNG)
// and before advertising; the caller prints bleGattPasskey() on serial.
// Returns false if NimBLE failed to start.
bool bleGattStart(const char* deviceName, BleRxFn rx, void* rxCtx);

uint32_t bleGattPasskey(void);

// True while one client is connected, encrypted, authenticated (MITM) and
// bonded, and it is the bonded owner.
bool bleGattSecure(void);

// Incremented on every connect and disconnect: requests from an earlier
// session must not be answered on a newer one.
uint32_t bleGattGeneration(void);

// Largest notification payload on the current link (ATT MTU - 3), >= 20.
size_t bleGattChunkSize(void);

// Sends one notification of at most bleGattChunkSize() bytes. false if the
// link is not secure, the chunk is too big, or NimBLE is out of buffers
// (the caller may retry shortly or drop the frame).
bool bleGattNotify(BleChannel channel, const uint8_t* data, size_t len);

// Value returned by a read of the State characteristic (one whole frame,
// header included; long reads are supported up to BLE_GATT_STATE_MAX).
#define BLE_GATT_STATE_MAX 1024
bool bleGattSetStateValue(const uint8_t* frame, size_t len);

// Number of stored bonds, and the recovery path (BOOT held 10 s at runtime):
// removes every bond and drops the current client.
int bleGattBondCount(void);
void bleGattClearBonds(void);

void bleGattGetStats(BleGattStats* out);

#ifdef __cplusplus
}
#endif
