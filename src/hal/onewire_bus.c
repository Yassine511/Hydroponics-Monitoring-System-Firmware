#include "hal/onewire_bus.h"

// TODO(WP2): implement (e.g. a C 1-Wire bit-bang on the GPIO HAL, or the
// ESP-IDF onewire_bus component). Bit timing critical sections must stay short.

bool oneWireBusBegin(OneWireBus* bus, int8_t pin) { bus->pin_ = pin; return false; }
bool oneWireBusReset(OneWireBus* bus) { (void)bus; return false; }
void oneWireBusWriteByte(OneWireBus* bus, uint8_t value) { (void)bus; (void)value; }
uint8_t oneWireBusReadByte(OneWireBus* bus) { (void)bus; return 0xFF; }
bool oneWireBusSearchFirst(OneWireBus* bus, uint8_t rom[8]) { (void)bus; (void)rom; return false; }
void oneWireBusSelect(OneWireBus* bus, const uint8_t rom[8]) { (void)bus; (void)rom; }

uint8_t oneWireBusCrc8(const uint8_t* data, size_t len) {
  // Dallas/Maxim CRC-8 (x^8 + x^5 + x^4 + 1), DS18B20 datasheet p.9.
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    uint8_t in = data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      uint8_t mix = (crc ^ in) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      in >>= 1;
    }
  }
  return crc;
}
