// Host unit tests for src/domain (pio test -e native). Architecture §17 stage 4.
// Domain code must stay free of Arduino/FreeRTOS so these run on a PC.
#include <unity.h>

#include "board_profile.h"
#include "domain/calibration.h"
#include "domain/ds18b20_codec.h"
#include "domain/reading.h"
#include "domain/sgp30_codec.h"
#include "domain/tds_codec.h"
#include "domain/ultrasonic.h"

void setUp(void) {}
void tearDown(void) {}

static void test_reset_reading_is_starting_and_not_actionable(void) {
  Reading r;
  readingReset(&r, SENSOR_BME280, METRIC_AIR_TEMPERATURE, UNIT_DEG_C);
  TEST_ASSERT_TRUE(r.health == HEALTH_STARTING);
  TEST_ASSERT_TRUE(r.sensor == SENSOR_BME280);
  TEST_ASSERT_FALSE(readingIsActionable(&r));
}

static void test_ok_reading_becomes_stale_after_limit(void) {
  Reading r;
  readingReset(&r, SENSOR_DS18B20, METRIC_WATER_TEMPERATURE, UNIT_DEG_C);
  r.health = HEALTH_OK;
  r.intervalEndMs = 1000;
  TEST_ASSERT_TRUE(readingHealthAt(&r, 2000, 1000) == HEALTH_OK);
  TEST_ASSERT_TRUE(readingHealthAt(&r, 2001, 1000) == HEALTH_STALE);
}

static void test_stale_reading_is_not_actionable(void) {
  Reading r;
  readingReset(&r, SENSOR_PH, METRIC_PH, UNIT_PH);
  r.health = HEALTH_OK;
  r.intervalEndMs = 1000;
  TEST_ASSERT_TRUE(readingIsActionableAt(&r, 4000, 3000));   // exactly at the limit
  TEST_ASSERT_FALSE(readingIsActionableAt(&r, 4001, 3000));  // stale
  TEST_ASSERT_TRUE(readingIsActionable(&r));                 // health-only check is unchanged
  r.health = HEALTH_UNCALIBRATED;
  TEST_ASSERT_FALSE(readingIsActionableAt(&r, 1000, 3000));  // fresh but not OK
}

static void test_fault_is_not_masked_by_staleness(void) {
  Reading r;
  readingReset(&r, SENSOR_PH, METRIC_PH, UNIT_PH);
  r.health = HEALTH_UNPOWERED;
  TEST_ASSERT_TRUE(readingHealthAt(&r, 100000, 1000) == HEALTH_UNPOWERED);
}

static void test_divider_recovery_uses_nominal_ratios(void) {
  // Power sense: 220/440 => x660/440. 3333 mV at GPIO8 -> 5000 mV external feed.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 4999.5f,
                           dividerRecoverMv(3333.0f, BOARD_POWER_SENSE_DIVIDER_TOP_OHM,
                                            BOARD_POWER_SENSE_DIVIDER_BOTTOM_OHM));
  // pH: 10k/15k => x25/15. 3000 mV at the pin -> 5000 mV at the module.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 5000.0f,
                           dividerRecoverMv(3000.0f, BOARD_PH_DIVIDER_TOP_OHM,
                                            BOARD_PH_DIVIDER_BOTTOM_OHM));
}

static void test_two_point_fit_maps_buffers(void) {
  // Module mV in pH 6.86 and 4.01 buffers, about -5.7 pH/V (tutorial).
  LinearCalibration c;
  TEST_ASSERT_TRUE(linearFromTwoPoints(2500.0f, 6.86f, 3000.0f, 4.01f, 1, &c));
  TEST_ASSERT_TRUE(c.valid);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.0057f, c.slope);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.86f, linearApply(&c, 2500.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.01f, linearApply(&c, 3000.0f));
  TEST_ASSERT_FALSE(linearFromTwoPoints(2500.0f, 6.86f, 2500.0f, 4.01f, 2, &c));
  TEST_ASSERT_FALSE(c.valid);
}

static void test_trimmed_mean_rejects_spikes(void) {
  uint32_t s[10] = {1500, 4000, 1502, 1498, 0, 1501, 1499, 1500, 3100, 1500};
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1500.333f, trimmedMeanU32(s, 10, 2));  // 1499..1502
  TEST_ASSERT_TRUE(s[0] == 0 && s[9] == 4000);  // sorted in place
  uint32_t few[4] = {1, 2, 3, 4};
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, trimmedMeanU32(few, 4, 2));
}

static void test_snapshot_reset_indexes_by_metric(void) {
  Snapshot s;
  snapshotReset(&s);
  for (uint8_t i = 0; i < METRIC_COUNT; ++i) {
    TEST_ASSERT_TRUE(s.readings[i].metric == (MetricId)i);
    TEST_ASSERT_TRUE(s.readings[i].health == HEALTH_STARTING);
  }
}

static void test_production_ready_requires_power_sense(void) {
  BoardValidation v = boardProfileValidate();
  if (!v.powerSenseAssigned || !BOARD_POWER_SENSE_VERIFIED) TEST_ASSERT_FALSE(v.productionReady);
}

static void test_sgp30_crc_matches_datasheet_vector(void) {
  const uint8_t beef[2] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_HEX8(0x92, sgp30Crc8(beef, 2));
}

static void test_sgp30_words_round_trip_and_reject_bad_crc(void) {
  uint8_t buf[6];
  sgp30PackWord(0x01A3, &buf[0]);  // CO2eq 419 ppm
  sgp30PackWord(0x0007, &buf[3]);  // TVOC 7 ppb
  uint16_t w[2] = {0, 0};
  TEST_ASSERT_TRUE(sgp30UnpackWords(buf, 2, w));
  TEST_ASSERT_EQUAL_HEX16(0x01A3, w[0]);
  TEST_ASSERT_EQUAL_HEX16(0x0007, w[1]);
  buf[4] ^= 0x01;  // one flipped bit in the second word
  TEST_ASSERT_FALSE(sgp30UnpackWords(buf, 2, w));
  const uint8_t floating[3] = {0xFF, 0xFF, 0xFF};  // released bus
  TEST_ASSERT_FALSE(sgp30UnpackWords(floating, 1, w));
}

static void test_sgp30_humidity_fixed_point(void) {
  TEST_ASSERT_EQUAL_HEX16(0x0F80, sgp30GramsToFixed8p8(15.5f));  // datasheet example
  TEST_ASSERT_EQUAL_HEX16(0x0000, sgp30GramsToFixed8p8(0.0f));
  TEST_ASSERT_EQUAL_HEX16(0x0001, sgp30GramsToFixed8p8(0.001f));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, sgp30GramsToFixed8p8(300.0f));
  // The datasheet formula gives 11.48 g/m3 at 25 degC / 50 %RH (its text rounds
  // this example to 11.8).
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 11.48f, sgp30AbsoluteHumidity(25.0f, 50.0f));
  TEST_ASSERT_TRUE(sgp30AbsoluteHumidity(25.0f, 120.0f) < 0.0f);
}

static void test_ultrasonic_echo_width_to_mm(void) {
  // 58.3 us per cm at 343 m/s (HC-SR04 guide, equation 3).
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1000.0f,
                           ultrasonicEchoUsToMm(5831, ULTRASONIC_NOMINAL_SPEED_MPS));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f,
                           ultrasonicEchoUsToMm(117, ULTRASONIC_NOMINAL_SPEED_MPS));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, ultrasonicEchoUsToMm(0, ULTRASONIC_NOMINAL_SPEED_MPS));
}

static void test_ultrasonic_range_is_2_to_400_cm(void) {
  TEST_ASSERT_TRUE(ultrasonicInRangeMm(20.0f));
  TEST_ASSERT_TRUE(ultrasonicInRangeMm(4000.0f));
  TEST_ASSERT_FALSE(ultrasonicInRangeMm(19.9f));
  TEST_ASSERT_FALSE(ultrasonicInRangeMm(4000.1f));
  TEST_ASSERT_FALSE(ultrasonicInRangeMm(0.0f));  // never a valid distance
}

static void test_tds_datasheet_curve(void) {
  // (133.42 - 255.86 + 857.39) * 0.5 at 1.0 V, 25 degC (SEN0244 sample code).
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, tdsCompensationCoefficient(25.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 367.475f, tdsPpmFromModuleMv(1000.0f, 1.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, tdsPpmFromModuleMv(0.0f, 1.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, tdsPpmFromModuleMv(1000.0f, 0.0f));
  // 2.3 V (top of the output range) is above the rated 1000 ppm.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1120.9f, tdsPpmFromModuleMv(2300.0f, 1.0f));
  TEST_ASSERT_TRUE(tdsPpmFromModuleMv(2300.0f, 1.0f) > TDS_FULL_SCALE_PPM);
}

static void test_tds_temperature_compensation(void) {
  // The conductivity (ppm) is compensated, not the voltage (GravityTDS).
  const float coefficient = tdsCompensationCoefficient(35.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.2f, coefficient);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 367.475f / 1.2f, tdsPpmFromModuleMv(1000.0f, coefficient));
  // Measured 24 Oct 2026, same tap water cold and warm: within ~2 % at 25 degC.
  const float cold = tdsPpmFromModuleMv(1234.0f, tdsCompensationCoefficient(11.44f));
  const float warm = tdsPpmFromModuleMv(1768.0f, tdsCompensationCoefficient(33.62f));
  TEST_ASSERT_FLOAT_WITHIN(0.03f * cold, cold, warm);
}

// Scratchpad with the given temperature register and 12-bit config, valid CRC.
static void makeScratchpad(int16_t raw, uint8_t sp[DS18B20_SCRATCHPAD_LEN]) {
  const uint8_t base[8] = {(uint8_t)(raw & 0xFF), (uint8_t)((uint16_t)raw >> 8), 0x4B, 0x46,
                           DS18B20_CONFIG_12BIT, 0xFF, 0x0C, 0x10};
  for (uint8_t i = 0; i < 8; ++i) sp[i] = base[i];
  sp[8] = oneWireCrc8(sp, 8);
}

static void test_onewire_crc_matches_maxim_vector(void) {
  // Maxim application note 27: ROM 02 1C B8 01 00 00 00 has CRC A2.
  const uint8_t rom[8] = {0x02, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xA2};
  TEST_ASSERT_EQUAL_HEX8(0xA2, oneWireCrc8(rom, 7));
  TEST_ASSERT_EQUAL_HEX8(0x00, oneWireCrc8(rom, 8));  // data + CRC -> 0
  TEST_ASSERT_FALSE(ds18b20RomValid(rom));  // family 02h, not a DS18B20
  uint8_t ds[8] = {0x28, 0xFF, 0x64, 0x1E, 0x0F, 0x4A, 0x3C, 0x00};
  ds[7] = oneWireCrc8(ds, 7);
  TEST_ASSERT_TRUE(ds18b20RomValid(ds));
  ds[3] ^= 0x01;
  TEST_ASSERT_FALSE(ds18b20RomValid(ds));
  const uint8_t zero[8] = {0};  // bus stuck low: CRC 0 matches, family does not
  TEST_ASSERT_FALSE(ds18b20RomValid(zero));
}

static void test_ds18b20_temperature_table(void) {
  // Datasheet Table 1.
  uint8_t sp[DS18B20_SCRATCHPAD_LEN];
  makeScratchpad(0x0191, sp);
  TEST_ASSERT_TRUE(ds18b20ScratchpadValid(sp));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 25.0625f, ds18b20RawToDegC(ds18b20RawTemperature(sp)));
  makeScratchpad((int16_t)0xFF5E, sp);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -10.125f, ds18b20RawToDegC(ds18b20RawTemperature(sp)));
  makeScratchpad((int16_t)0xFC90, sp);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -55.0f, ds18b20RawToDegC(ds18b20RawTemperature(sp)));
  makeScratchpad(0x07D0, sp);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 125.0f, ds18b20RawToDegC(ds18b20RawTemperature(sp)));
  makeScratchpad(DS18B20_POWER_ON_RAW, sp);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 85.0f, ds18b20RawToDegC(ds18b20RawTemperature(sp)));
}

static void test_ds18b20_scratchpad_checks(void) {
  uint8_t sp[DS18B20_SCRATCHPAD_LEN];
  makeScratchpad(0x0191, sp);
  sp[0] ^= 0x01;  // one bit flipped on the wire
  TEST_ASSERT_FALSE(ds18b20ScratchpadValid(sp));
  const uint8_t zero[DS18B20_SCRATCHPAD_LEN] = {0};  // CRC 0 matches, config does not
  TEST_ASSERT_FALSE(ds18b20ScratchpadValid(zero));
  uint8_t ff[DS18B20_SCRATCHPAD_LEN];  // floating / no device
  for (uint8_t i = 0; i < DS18B20_SCRATCHPAD_LEN; ++i) ff[i] = 0xFF;
  TEST_ASSERT_FALSE(ds18b20ScratchpadValid(ff));
  TEST_ASSERT_TRUE(ds18b20Is12Bit(0x7F));
  TEST_ASSERT_FALSE(ds18b20Is12Bit(0x5F));  // 11-bit
  TEST_ASSERT_FALSE(ds18b20Is12Bit(0x1F));  // 9-bit
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_reset_reading_is_starting_and_not_actionable);
  RUN_TEST(test_ok_reading_becomes_stale_after_limit);
  RUN_TEST(test_stale_reading_is_not_actionable);
  RUN_TEST(test_fault_is_not_masked_by_staleness);
  RUN_TEST(test_divider_recovery_uses_nominal_ratios);
  RUN_TEST(test_two_point_fit_maps_buffers);
  RUN_TEST(test_trimmed_mean_rejects_spikes);
  RUN_TEST(test_snapshot_reset_indexes_by_metric);
  RUN_TEST(test_production_ready_requires_power_sense);
  RUN_TEST(test_sgp30_crc_matches_datasheet_vector);
  RUN_TEST(test_sgp30_words_round_trip_and_reject_bad_crc);
  RUN_TEST(test_sgp30_humidity_fixed_point);
  RUN_TEST(test_ultrasonic_echo_width_to_mm);
  RUN_TEST(test_ultrasonic_range_is_2_to_400_cm);
  RUN_TEST(test_tds_datasheet_curve);
  RUN_TEST(test_tds_temperature_compensation);
  RUN_TEST(test_onewire_crc_matches_maxim_vector);
  RUN_TEST(test_ds18b20_temperature_table);
  RUN_TEST(test_ds18b20_scratchpad_checks);
  return UNITY_END();
}
