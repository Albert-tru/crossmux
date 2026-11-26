#pragma once

#include <cstdint>

// Pure arithmetic for crediting local reading time to WeRead. Header-only and free of storage or
// network access so it can be unit-tested on the host without hardware.
//
// Model: the local reading-stats store keeps a lifetime total per book. We persist a per-book
// watermark of whole seconds already credited to WeRead. What is still owed is therefore always
// (total - watermark), so a failed or interrupted upload is harmless: the watermark only moves after
// the server accepted a report, so nothing is lost and nothing is ever sent twice.
namespace WeReadReadingTime {

// WeRead's own web reader reports every ~30 s, each report carrying the seconds elapsed since the
// previous one. A single report carrying hours is unlike anything the server normally sees, so
// large backlogs are drained in bounded steps rather than in one request. 30 s matches that cadence;
// a device log showed that 60 s reports were credited at only about half their claimed value.
constexpr uint32_t kMaxSecondsPerReport = 30;

// Upper bound on report requests in one sync, so a long backlog cannot keep the radio on for many
// minutes. 480 reports of up to 30 s drain up to 4 hours per sync; anything left stays owed and goes
// out on the next sync because the watermark only moves on accepted reports.
constexpr uint32_t kMaxReportsPerSync = 480;

// Callers store the per-sync total in 16 bits; keep that true if either cap above is ever raised.
static_assert(kMaxReportsPerSync * kMaxSecondsPerReport <= UINT16_MAX, "per-sync time budget must fit 16 bits");

// A book with no watermark has never been synced for time. Crediting its whole local history in one
// burst would look nothing like normal reading, so only the most recent hour is credited and the
// older remainder is forgiven (the watermark starts that far behind the total).
constexpr uint32_t kFirstSyncBackfillSeconds = 3600;

// Sub-5 s remainders are not worth a round trip; they stay owed and are folded into the next sync
// automatically because the watermark does not move.
constexpr uint32_t kMinSecondsToReport = 5;

constexpr uint32_t secondsFromMs(const uint64_t readingMs) {
  const uint64_t seconds = readingMs / 1000ULL;
  return seconds > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(seconds);
}

// Seconds still owed to WeRead. A watermark ahead of the local total (stats were reset, adjusted
// down, or restored from an older backup) yields zero rather than wrapping.
constexpr uint32_t owedSeconds(const uint32_t totalSeconds, const uint32_t reportedSeconds) {
  return totalSeconds > reportedSeconds ? totalSeconds - reportedSeconds : 0;
}

// How many seconds the next report should carry. Zero means "send position only".
constexpr uint32_t nextReportSeconds(const uint32_t owed) {
  if (owed < kMinSecondsToReport) return 0;
  return owed > kMaxSecondsPerReport ? kMaxSecondsPerReport : owed;
}

// Watermark for a book that has none yet: leave at most kFirstSyncBackfillSeconds owed.
constexpr uint32_t initialWatermark(const uint32_t totalSeconds) {
  return totalSeconds > kFirstSyncBackfillSeconds ? totalSeconds - kFirstSyncBackfillSeconds : 0;
}

// New watermark after the server accepted a report of `sentSeconds`. Saturating so a corrupt
// watermark can never wrap around and re-credit the whole history.
constexpr uint32_t advancedWatermark(const uint32_t reportedSeconds, const uint32_t sentSeconds) {
  return sentSeconds > UINT32_MAX - reportedSeconds ? UINT32_MAX : reportedSeconds + sentSeconds;
}

}  // namespace WeReadReadingTime
