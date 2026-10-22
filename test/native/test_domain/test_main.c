// Host unit tests for src/domain (pio test -e native). Architecture §17 stage 4.
// Domain code must stay free of Arduino/FreeRTOS so these run on a PC.
#include <unity.h>

#include "board_profile.h"
#include "domain/calibration.h"
#include "domain/reading.h"
#include "domain/sgp30_codec.h"

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
  return UNITY_END();
}
