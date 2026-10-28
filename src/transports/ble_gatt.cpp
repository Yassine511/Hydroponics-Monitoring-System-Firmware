// NimBLE-Arduino implementation of ble_gatt.h. The only C++ file in the
// project: NimBLE-Arduino is a C++ library, and ESP-Demo already proved this
// version on espressif32@6.12.0 / ESP32-S3. Everything it exports is C.
//
// Compiled only with -D FW_ENABLE_BLE (the production envs), so the bring-up
// images do not link NimBLE.
#ifdef FW_ENABLE_BLE
#include "transports/ble_gatt.h"

#include <NimBLEDevice.h>

#include <esp_system.h>

#include <atomic>
#include <string.h>

namespace {

constexpr const char* kServiceUuid = "98a90f00-346e-4e65-9cbd-7687685bdc01";
constexpr const char* kStateUuid = "98a90f01-346e-4e65-9cbd-7687685bdc01";
constexpr const char* kTelemetryUuid = "98a90f02-346e-4e65-9cbd-7687685bdc01";
constexpr const char* kCommandUuid = "98a90f03-346e-4e65-9cbd-7687685bdc01";
constexpr const char* kResultUuid = "98a90f04-346e-4e65-9cbd-7687685bdc01";
constexpr const char* kAuthUuid = "98a90f05-346e-4e65-9cbd-7687685bdc01";
constexpr const char* kAuthValue = "HYDRO_SECURE_V1";
constexpr uint16_t kPreferredMtu = 247;
constexpr size_t kMinChunk = 20;

NimBLEServer* s_server = nullptr;
NimBLECharacteristic* s_channel[BLE_CHANNEL_COUNT] = {};
NimBLECharacteristic* s_command = nullptr;
BleRxFn s_rx = nullptr;
void* s_rxCtx = nullptr;
uint32_t s_passkey = 0;

std::atomic<bool> s_secure{false};
std::atomic<uint32_t> s_generation{0};
std::atomic<uint16_t> s_conn{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint16_t> s_mtu{23};

portMUX_TYPE s_statsMux = portMUX_INITIALIZER_UNLOCKED;
BleGattStats s_stats = {};

template <typename F>
void stats(F update) {
  portENTER_CRITICAL(&s_statsMux);
  update(s_stats);
  portEXIT_CRITICAL(&s_statsMux);
}

bool linkIsSecure(const NimBLEConnInfo& info) {
  return info.isEncrypted() && info.isAuthenticated() && info.isBonded() && info.getSecKeySize() >= 16;
}

class ServerCallbacks final : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    if (server->getConnectedCount() > 1) {  // one client at a time
      server->disconnect(info.getConnHandle());
      return;
    }
    s_secure.store(false);
    s_conn.store(info.getConnHandle());
    s_mtu.store(23);
    s_generation.fetch_add(1);
    stats([](BleGattStats& s) { ++s.connects; });
    // Ask for security at once: Windows and BlueZ then pair before discovery.
    NimBLEDevice::startSecurity(info.getConnHandle());
  }

  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
    if (info.getConnHandle() == s_conn.load()) {
      s_secure.store(false);
      s_conn.store(BLE_HS_CONN_HANDLE_NONE);
      s_generation.fetch_add(1);
    }
    stats([reason](BleGattStats& s) {
      ++s.disconnects;
      s.lastDisconnectReason = reason;
    });
    NimBLEDevice::startAdvertising();
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { s_mtu.store(mtu); }

  uint32_t onPassKeyDisplay() override { return s_passkey; }

  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    bool secure = linkIsSecure(info);
    // Single owner: the first bond is the owner. A second bond means another
    // computer just paired: remove it and drop that client.
    if (secure && NimBLEDevice::getNumBonds() > 1) {
      NimBLEDevice::deleteBond(info.getIdAddress());
      secure = false;
    }
    s_secure.store(secure);
    if (secure) {
      s_mtu.store(info.getMTU());
      stats([](BleGattStats& s) { ++s.pairingsAccepted; });
    } else {
      stats([](BleGattStats& s) { ++s.pairingsRejected; });
      s_server->disconnect(info.getConnHandle());
    }
  }
};

class CommandCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
    if (!s_secure.load() || !linkIsSecure(info)) {
      stats([](BleGattStats& s) { ++s.rxDropped; });
      s_server->disconnect(info.getConnHandle());
      return;
    }
    const NimBLEAttValue value = characteristic->getValue();
    if (s_rx != nullptr && value.length() > 0) s_rx(value.data(), value.length(), s_generation.load(), s_rxCtx);
  }
};

ServerCallbacks s_serverCallbacks;
CommandCallbacks s_commandCallbacks;

}  // namespace

extern "C" bool bleGattStart(const char* deviceName, BleRxFn rx, void* rxCtx) {
  if (s_server != nullptr) return true;
  s_rx = rx;
  s_rxCtx = rxCtx;
  if (!NimBLEDevice::init(deviceName)) return false;
  // The controller is running now, so the hardware RNG is seeded from RF noise.
  s_passkey = 100000u + esp_random() % 900000u;
  NimBLEDevice::setSecurityAuth(true, true, true);  // bonding, MITM, LE Secure Connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(s_passkey);
  NimBLEDevice::setMTU(kPreferredMtu);

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(&s_serverCallbacks);
  NimBLEService* service = s_server->createService(kServiceUuid);
  // *_AUTHEN: the stack itself refuses access without an authenticated (MITM) link.
  s_channel[BLE_CHANNEL_STATE] = service->createCharacteristic(
      kStateUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::NOTIFY,
      BLE_GATT_STATE_MAX);
  s_channel[BLE_CHANNEL_TELEMETRY] = service->createCharacteristic(
      kTelemetryUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN, 20);
  s_command = service->createCharacteristic(kCommandUuid,
                                            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN, 512);
  s_command->setCallbacks(&s_commandCallbacks);
  s_channel[BLE_CHANNEL_RESULT] = service->createCharacteristic(
      kResultUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN, 20);
  NimBLECharacteristic* auth =
      service->createCharacteristic(kAuthUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN, 32);
  auth->setValue(kAuthValue);
  if (!s_server->start()) return false;

  // 31-byte advertising packet: flags + 128-bit service UUID (21 B) leave no room
  // for the name, so the name goes in the scan response. Order matters:
  // setName() only targets the scan response once it is enabled.
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->enableScanResponse(true);
  advertising->setName(deviceName);
  advertising->addServiceUUID(kServiceUuid);
  return advertising->start();
}

extern "C" uint32_t bleGattPasskey(void) { return s_passkey; }

extern "C" bool bleGattSecure(void) { return s_secure.load(); }

extern "C" uint32_t bleGattGeneration(void) { return s_generation.load(); }

extern "C" size_t bleGattChunkSize(void) {
  const size_t mtu = s_mtu.load();
  return mtu > kMinChunk + 3 ? mtu - 3 : kMinChunk;
}

extern "C" bool bleGattNotify(BleChannel channel, const uint8_t* data, size_t len) {
  const uint16_t conn = s_conn.load();
  if (channel >= BLE_CHANNEL_COUNT || !s_secure.load() || conn == BLE_HS_CONN_HANDLE_NONE ||
      len == 0 || len > bleGattChunkSize()) {
    return false;
  }
  if (!s_channel[channel]->notify(data, len, conn)) {
    stats([](BleGattStats& s) { ++s.notifyFailures; });
    return false;
  }
  return true;
}

extern "C" bool bleGattSetStateValue(const uint8_t* frame, size_t len) {
  if (s_channel[BLE_CHANNEL_STATE] == nullptr || len > BLE_GATT_STATE_MAX) return false;
  s_channel[BLE_CHANNEL_STATE]->setValue(frame, len);
  return true;
}

extern "C" int bleGattBondCount(void) { return s_server != nullptr ? NimBLEDevice::getNumBonds() : 0; }

extern "C" void bleGattClearBonds(void) {
  if (s_server == nullptr) return;
  const uint16_t conn = s_conn.load();
  if (conn != BLE_HS_CONN_HANDLE_NONE) s_server->disconnect(conn);
  NimBLEDevice::deleteAllBonds();
}

extern "C" void bleGattGetStats(BleGattStats* out) {
  portENTER_CRITICAL(&s_statsMux);
  *out = s_stats;
  portEXIT_CRITICAL(&s_statsMux);
}

#endif  // FW_ENABLE_BLE
