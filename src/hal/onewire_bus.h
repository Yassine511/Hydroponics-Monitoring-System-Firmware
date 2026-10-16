// 1-Wire bus adapter (HAL). Work package WP2 (with DS18B20).
// Externally powered DS18B20 on BOARD_PIN_ONE_WIRE (4.7 kOhm pull-up).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Treat the members as private to onewire_bus.c. Initialise with ONE_WIRE_BUS_INIT.
typedef struct OneWireBus {
  int8_t pin_;
} OneWireBus;

#define ONE_WIRE_BUS_INIT {-1}

bool oneWireBusBegin(OneWireBus* bus, int8_t pin);
bool oneWireBusReset(OneWireBus* bus);  // true = presence pulse detected
void oneWireBusWriteByte(OneWireBus* bus, uint8_t value);
uint8_t oneWireBusReadByte(OneWireBus* bus);
bool oneWireBusSearchFirst(OneWireBus* bus, uint8_t rom[8]);
void oneWireBusSelect(OneWireBus* bus, const uint8_t rom[8]);
uint8_t oneWireBusCrc8(const uint8_t* data, size_t len);
