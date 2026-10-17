// Board profile: pins, rails and divider values for the ESP32-S3-WROOM-1 N16R8
// board (was ESP32-WROOM-32D; architecture decision D02 superseded).
// Source: Firmware Architecture §3 (Pin Layout.drawio + Electrical System
// Diagram). FROZEN CONTRACT - change only with a hardware-verified reason.
//
// Pure C (no Arduino headers) so domain code and native tests can use it.
// Anything not confirmed on the hardware stays BOARD_PIN_UNASSIGNED and the
// corresponding feature stays disabled (see boardProfileValidate()).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BOARD_PIN_UNASSIGNED ((int8_t)-1)

typedef uint8_t Rail;
enum {
  RAIL_3V3 = 0,         // retained in low power
  RAIL_5V_SWITCHED = 1  // removed in low power
};

// Bit mask used by the acquisition lease (allowed rails for a worker).
#define BOARD_RAIL_MASK_3V3 ((uint8_t)(1u << 0))
#define BOARD_RAIL_MASK_5V ((uint8_t)(1u << 1))

// ---- BME280 (3.3 V, I2C, GY-BME280-3.3 module with onboard pull-ups) --------
// S3 has no GPIO22; SCL moved to GPIO47 (wired by hand, WP1).
#define BOARD_PIN_I2C_SDA ((int8_t)21)
#define BOARD_PIN_I2C_SCL ((int8_t)47)
#define BOARD_I2C_CLOCK_HZ ((uint32_t)400000)
#define BOARD_BME280_I2C_ADDRESS ((uint8_t)0x76)      // SDO = GND
#define BOARD_BME280_I2C_ADDRESS_ALT ((uint8_t)0x77)  // SDO = VDDIO

// TODO(S3): the pins below are still the WROOM-32D mapping and are INVALID on
// the S3 (GPIO26-32 = flash/PSRAM, GPIO33-37 = octal PSRAM on R8, ADC1 is
// GPIO1-10). Remap before WP2-WP6. GPIO4 (1-Wire) remains valid. The pH pin
// has been remapped (see below).

// ---- DS18B20 (3.3 V, 1-Wire, external supply, 4.7 kOhm pull-up) -------------
#define BOARD_PIN_ONE_WIRE ((int8_t)4)

// ---- DFR0026 ambient light (3.3 V, ADC1_CH4) --------------------------------
#define BOARD_PIN_LIGHT_ADC ((int8_t)32)

// ---- SEN0244 TDS (switched 5 V, ADC1_CH5, 0-2.3 V nominal) ------------------
#define BOARD_PIN_TDS_ADC ((int8_t)33)

// ---- pH-4502C (switched 5 V, ADC1_CH6, divider 10k top / 15k bottom) --------
// S3 remap (WP5, Oct 2026): PO -> 10k/15k -> GPIO7 (ADC1_CH6) + 1 uF to GND,
// as wired and drawn in Pin Layout.drawio (16 Oct 2026). Was GPIO34 on the D.
#define BOARD_PIN_PH_ADC ((int8_t)7)
#define BOARD_PH_DIVIDER_TOP_OHM ((uint32_t)10000)
#define BOARD_PH_DIVIDER_BOTTOM_OHM ((uint32_t)15000)

// ---- MQ-7 (switched 5 V, ADC1_CH7, divider 22k top / 18k bottom) ------------
#define BOARD_PIN_MQ7_ADC ((int8_t)35)
#define BOARD_MQ7_DIVIDER_TOP_OHM ((uint32_t)22000)
#define BOARD_MQ7_DIVIDER_BOTTOM_OHM ((uint32_t)18000)
#define BOARD_PIN_MQ7_HEATER BOARD_PIN_UNASSIGNED  // heater control unresolved

// ---- HC-SR04 (switched 5 V, ECHO through 1k/2k divider) ---------------------
#define BOARD_PIN_HCSR04_TRIG ((int8_t)26)
#define BOARD_PIN_HCSR04_ECHO ((int8_t)27)

// ---- Open hardware gates (§3, §11, §18) - NOT assigned on the drawing --------
#define BOARD_PIN_EXTERNAL_POWER_SENSE BOARD_PIN_UNASSIGNED
#define BOARD_PIN_RAIL_5V_FEEDBACK BOARD_PIN_UNASSIGNED
#define BOARD_PIN_RELAY1 BOARD_PIN_UNASSIGNED
#define BOARD_RELAY1_ACTIVE_LOW false  // unknown until verified

static inline bool boardPinAssigned(int8_t pin) { return pin != BOARD_PIN_UNASSIGNED; }

typedef struct BoardValidation {
  bool powerSenseAssigned;
  bool railFeedbackAssigned;
  bool relayAssigned;
  bool mq7HeaterAssigned;
  bool productionReady;  // false => report a non-production build status
} BoardValidation;

static inline BoardValidation boardProfileValidate(void) {
  BoardValidation v;
  v.powerSenseAssigned = boardPinAssigned(BOARD_PIN_EXTERNAL_POWER_SENSE);
  v.railFeedbackAssigned = boardPinAssigned(BOARD_PIN_RAIL_5V_FEEDBACK);
  v.relayAssigned = boardPinAssigned(BOARD_PIN_RELAY1);
  v.mq7HeaterAssigned = boardPinAssigned(BOARD_PIN_MQ7_HEATER);
  v.productionReady = v.powerSenseAssigned;  // minimum gate for power modes
  return v;
}
