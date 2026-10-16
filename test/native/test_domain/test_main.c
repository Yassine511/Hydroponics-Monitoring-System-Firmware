// Host unit tests for src/domain (pio test -e native). Architecture §17 stage 4.
// Domain code must stay free of Arduino/FreeRTOS so these run on a PC.
#include <unity.h>

#include "board_profile.h"
#include "domain/calibration.h"
#include "domain/reading.h"

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

static void test_fault_is_not_masked_by_staleness(void) {
  Reading r;
  readingReset(&r, SENSOR_PH, METRIC_PH, UNIT_PH);
  r.health = HEALTH_UNPOWERED;
  TEST_ASSERT_TRUE(readingHealthAt(&r, 100000, 1000) == HEALTH_UNPOWERED);
}

static void test_divider_recovery_uses_nominal_ratios(void) {
  // MQ-7: 22k/18k => x40/18. 1800 mV at the pin -> 4000 mV at the module.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 4000.0f,
                           dividerRecoverMv(1800.0f, BOARD_MQ7_DIVIDER_TOP_OHM,
                                            BOARD_MQ7_DIVIDER_BOTTOM_OHM));
  // pH: 10k/15k => x25/15. 3000 mV at the pin -> 5000 mV at the module.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 5000.0f,
                           dividerRecoverMv(3000.0f, BOARD_PH_DIVIDER_TOP_OHM,
                                            BOARD_PH_DIVIDER_BOTTOM_OHM));
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
  if (!v.powerSenseAssigned) TEST_ASSERT_FALSE(v.productionReady);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_reset_reading_is_starting_and_not_actionable);
  RUN_TEST(test_ok_reading_becomes_stale_after_limit);
  RUN_TEST(test_fault_is_not_masked_by_staleness);
  RUN_TEST(test_divider_recovery_uses_nominal_ratios);
  RUN_TEST(test_snapshot_reset_indexes_by_metric);
  RUN_TEST(test_production_ready_requires_power_sense);
  return UNITY_END();
}
