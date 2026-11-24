/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Free of Velox, so `tests/` can build it outside the build-env image. See tests/README.md.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>

#include "connector/search_lib/ClpStorageCounters.h"

namespace facebook::velox::connector::clp::search_lib {

/// The timed phases of a scan. The metric names and their meaning are listed in
/// docs/runtime-metrics.md.
///
/// kAddSplit, kCloseArchive and kNext are the outer phases: they do not overlap, and together they
/// cover every call that the TableScan makes into the data source. kParseQuery lies inside
/// kAddSplit; the phases from kLoadSplit to kCreateVector lie inside kNext, and those from
/// kOpenArchive to kQueryRunnerInit inside kLoadSplit. The lazy loads run when an operator
/// downstream first reads a column, outside the outer phases.
enum class ScanPhase : uint8_t {
  kAddSplit = 0,
  kCloseArchive,
  kNext,
  kParseQuery,
  kLoadSplit,
  kOpenArchive,
  kTimestampIndexAndSchemaMatch,
  kProjection,
  kReadTableMetadata,
  kLoadDictionaries,
  kOpenPackedStreams,
  kQueryRunnerInit,
  kSchemaInit,
  kReadTable,
  kFilterInit,
  kFilter,
  kCreateVector,
  kLazyLoadJson,
  kLazyLoadColumn,
  kNumPhases
};

enum class ScanCounter : uint8_t {
  kSplits = 0,
  kSchemasInArchive,
  kSchemasMatched,
  kTablesRead,
  kRowsScanned,
  kRowsMatched,
  kJsonRows,
  kJsonBytes,
  kValuesMaterialized,
  kNumCounters
};

enum class ScanMetricUnit : uint8_t { kNone, kNanos, kBytes };

/// A metric over the splits of one scan: `sum` is the total, while `count`, `min` and `max`
/// describe the per-split values. It maps one to one onto Velox's `RuntimeMetric`.
struct ScanMetric {
  ScanMetricUnit unit{ScanMetricUnit::kNone};
  int64_t sum{0};
  uint64_t count{0};
  int64_t min{std::numeric_limits<int64_t>::max()};
  int64_t max{std::numeric_limits<int64_t>::min()};

  void add(uint64_t value);
};

/// A reading of the calling thread's clocks and storage counters. A phase's values are the
/// difference between two marks made on the same thread.
struct ScanMark {
  /// Monotonic wall clock.
  uint64_t wallNanos{0};
  /// The calling thread's CPU time.
  uint64_t cpuNanos{0};
  ClpStorageCounters storage;

  static ScanMark now();
};

/// The timers and counters of one data source, that is, of one driver's scan. Measurement only:
/// nothing in the connector reads these values to make a decision.
///
/// A data source is used by one thread at a time, so the values are plain integers. They are kept
/// per split and folded into the totals when the split's cursor has been destroyed.
class ClpScanStats {
 public:
  static constexpr size_t kNumPhases = static_cast<size_t>(ScanPhase::kNumPhases);
  static constexpr size_t kNumCounters = static_cast<size_t>(ScanCounter::kNumCounters);

  /// Adds the interval between two marks to a phase of the split in progress.
  void addPhase(ScanPhase phase, const ScanMark& begin, const ScanMark& end);

  void addCounter(ScanCounter counter, uint64_t value);

  /// Starts a split's values; counts the split.
  void beginSplit();

  [[nodiscard]] bool splitOpen() const {
    return splitOpen_;
  }

  /// Folds the split in progress into the totals.
  void finishSplit();

  /// @return The split in progress as a JSON object of its nonzero values, keyed by metric name.
  [[nodiscard]] std::string describeSplit() const;

  /// @return Every metric, keyed by name: the finished splits, with the split in progress (if any)
  /// as one more observation.
  [[nodiscard]] std::unordered_map<std::string, ScanMetric> metrics() const;

  /// @return The archive bytes taken from storage so far, the split in progress included.
  [[nodiscard]] uint64_t bytesConsumed() const;

 private:
  // A phase's per-split values, in this order.
  enum PhaseValue : uint8_t { kWall = 0, kCpu, kStorageWait, kCalls, kNumPhaseValues };
  static constexpr size_t kNumStorageValues = 7;

  struct Split {
    std::array<std::array<uint64_t, kNumPhaseValues>, kNumPhases> phases{};
    std::array<uint64_t, kNumCounters> counters{};
    // The ClpStorageCounters fields, in declaration order.
    std::array<uint64_t, kNumStorageValues> storage{};
  };

  struct Totals {
    std::array<std::array<ScanMetric, kNumPhaseValues>, kNumPhases> phases{};
    std::array<ScanMetric, kNumCounters> counters{};
    std::array<ScanMetric, kNumStorageValues> storage{};
  };

  /// Calls `f(name, unit, value)` for each value of `split` that is an observation: every counter
  /// and storage value, and the values of the phases that ran in the split.
  template <typename F>
  static void forEachObservation(const Split& split, F&& f);

  static void fold(const Split& split, Totals& totals);

  bool splitOpen_{false};
  uint64_t bytesConsumedFinished_{0};
  Split split_;
  Totals totals_;
};

/// Adds the time from construction to destruction to a phase. Does nothing without stats.
class ScanPhaseTimer {
 public:
  ScanPhaseTimer(ClpScanStats* stats, ScanPhase phase);
  ~ScanPhaseTimer();

  ScanPhaseTimer(const ScanPhaseTimer&) = delete;
  ScanPhaseTimer& operator=(const ScanPhaseTimer&) = delete;

 private:
  ClpScanStats* const stats_;
  const ScanPhase phase_;
  ScanMark begin_;
};

/// Times consecutive pieces of a function: each `mark` adds the time since the previous mark (or
/// since construction) to a phase. Does nothing without stats.
class ScanPhaseLap {
 public:
  explicit ScanPhaseLap(ClpScanStats* stats);

  void mark(ScanPhase phase);

 private:
  ClpScanStats* const stats_;
  ScanMark last_;
};

} // namespace facebook::velox::connector::clp::search_lib
