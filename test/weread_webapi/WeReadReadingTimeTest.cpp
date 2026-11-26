#include <gtest/gtest.h>

#include "WeReadReadingTime.h"

using namespace WeReadReadingTime;

TEST(WeReadReadingTime, MillisecondsBecomeWholeSecondsAndSaturate) {
  EXPECT_EQ(secondsFromMs(0), 0u);
  EXPECT_EQ(secondsFromMs(90'999), 90u);  // sub-second remainder is never rounded up
  EXPECT_EQ(secondsFromMs(UINT64_MAX), UINT32_MAX);
}

TEST(WeReadReadingTime, OwedIsTotalMinusWatermark) {
  EXPECT_EQ(owedSeconds(0, 0), 0u);
  EXPECT_EQ(owedSeconds(90, 0), 90u);
  EXPECT_EQ(owedSeconds(90, 30), 60u);
}

TEST(WeReadReadingTime, WatermarkAheadOfTotalNeverWraps) {
  // Stats were reset or adjusted down after a report: owe nothing, never a huge unsigned value.
  EXPECT_EQ(owedSeconds(10, 500), 0u);
  EXPECT_EQ(owedSeconds(0, UINT32_MAX), 0u);
}

TEST(WeReadReadingTime, TinyRemaindersAreHeldBack) {
  EXPECT_EQ(nextReportSeconds(0), 0u);
  EXPECT_EQ(nextReportSeconds(kMinSecondsToReport - 1), 0u);
  EXPECT_EQ(nextReportSeconds(kMinSecondsToReport), kMinSecondsToReport);
}

TEST(WeReadReadingTime, LargeBacklogIsCappedPerReport) {
  EXPECT_EQ(nextReportSeconds(kMaxSecondsPerReport), kMaxSecondsPerReport);
  EXPECT_EQ(nextReportSeconds(kMaxSecondsPerReport + 1), kMaxSecondsPerReport);
  EXPECT_EQ(nextReportSeconds(3 * 3600), kMaxSecondsPerReport);
}

TEST(WeReadReadingTime, WatermarkAdvancesAndSaturates) {
  EXPECT_EQ(advancedWatermark(100, 30), 130u);
  EXPECT_EQ(advancedWatermark(0, 0), 0u);
  EXPECT_EQ(advancedWatermark(UINT32_MAX - 5, 60), UINT32_MAX);
  EXPECT_EQ(advancedWatermark(UINT32_MAX, 1), UINT32_MAX);
}

TEST(WeReadReadingTime, RepeatedReportsDrainBacklogExactlyOnce) {
  // 3 hours owed, sent 30 s at a time: every second goes out exactly once.
  const uint32_t total = 3 * 3600;
  uint32_t watermark = 0;
  uint64_t sent = 0;
  for (int i = 0; i < 1000; ++i) {
    const uint32_t step = nextReportSeconds(owedSeconds(total, watermark));
    if (step == 0) break;
    sent += step;
    watermark = advancedWatermark(watermark, step);
  }
  EXPECT_EQ(sent, total);
  EXPECT_EQ(owedSeconds(total, watermark), 0u);
}

TEST(WeReadReadingTime, FirstSyncForgivesHistoryOlderThanTheBackfillWindow) {
  const uint32_t fiveHours = 5 * 3600;
  EXPECT_EQ(owedSeconds(fiveHours, initialWatermark(fiveHours)), kFirstSyncBackfillSeconds);
  // A short history is credited in full.
  EXPECT_EQ(initialWatermark(20 * 60), 0u);
  EXPECT_EQ(owedSeconds(20 * 60, initialWatermark(20 * 60)), 20u * 60u);
}

TEST(WeReadReadingTime, OneSyncDrainsAtMostTheDocumentedBudget) {
  const uint32_t total = 10 * 3600;
  uint32_t watermark = 0;
  uint32_t reports = 0;
  while (reports < kMaxReportsPerSync) {
    const uint32_t step = nextReportSeconds(owedSeconds(total, watermark));
    if (step == 0) break;
    watermark = advancedWatermark(watermark, step);
    ++reports;
  }
  EXPECT_EQ(reports, kMaxReportsPerSync);
  EXPECT_EQ(watermark, kMaxReportsPerSync * kMaxSecondsPerReport);
  EXPECT_GT(owedSeconds(total, watermark), 0u);  // the rest waits for the next sync
}
