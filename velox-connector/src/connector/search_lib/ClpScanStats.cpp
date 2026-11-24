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

#include "connector/search_lib/ClpScanStats.h"

#include <time.h>

#include <algorithm>
#include <chrono>
#include <string_view>

namespace facebook::velox::connector::clp::search_lib {

namespace {

enum class Level : uint8_t {
  // Disjoint, and together they cover the data source's calls from the TableScan.
  kOuter,
  // Inside an outer phase.
  kInner,
  // Outside the outer phases.
  kDetached,
};

struct PhaseInfo {
  std::string_view name;
  Level level;
  // Whether the phase can read from storage, and so reports its storage wait.
  bool storage;
  // Whether the phase runs more than once per split, and so reports its calls.
  bool repeated;
};

constexpr std::array<PhaseInfo, ClpScanStats::kNumPhases> kPhases{{
    {"AddSplit", Level::kOuter, false, false},
    {"CloseArchive", Level::kOuter, false, false},
    {"Next", Level::kOuter, true, true},
    {"ParseQuery", Level::kInner, false, false},
    {"LoadSplit", Level::kInner, true, false},
    {"OpenArchive", Level::kInner, true, false},
    {"TimestampIndexAndSchemaMatch", Level::kInner, false, false},
    {"Projection", Level::kInner, false, false},
    {"ReadTableMetadata", Level::kInner, true, false},
    {"LoadDictionaries", Level::kInner, true, false},
    {"OpenPackedStreams", Level::kInner, true, false},
    {"QueryRunnerInit", Level::kInner, false, false},
    {"SchemaInit", Level::kInner, false, true},
    {"ReadTable", Level::kInner, true, true},
    {"FilterInit", Level::kInner, false, true},
    {"Filter", Level::kInner, false, true},
    {"CreateVector", Level::kInner, false, true},
    {"LazyLoadJson", Level::kDetached, false, true},
    {"LazyLoadColumn", Level::kDetached, false, true},
}};

struct NamedValue {
  std::string_view name;
  ScanMetricUnit unit;
};

constexpr std::array<NamedValue, ClpScanStats::kNumCounters> kCounters{{
    {"clpSplits", ScanMetricUnit::kNone},
    {"clpSchemasInArchive", ScanMetricUnit::kNone},
    {"clpSchemasMatched", ScanMetricUnit::kNone},
    {"clpTablesRead", ScanMetricUnit::kNone},
    {"clpRowsScanned", ScanMetricUnit::kNone},
    {"clpRowsMatched", ScanMetricUnit::kNone},
    {"clpJsonRows", ScanMetricUnit::kNone},
    {"clpJsonBytes", ScanMetricUnit::kBytes},
    {"clpValuesMaterialized", ScanMetricUnit::kNone},
}};

// In the order of the ClpStorageCounters fields.
constexpr std::array<NamedValue, 7> kStorage{{
    {"clpStorageReadersOpened", ScanMetricUnit::kNone},
    {"clpStorageReadersClosed", ScanMetricUnit::kNone},
    {"clpStorageCalls", ScanMetricUnit::kNone},
    {"clpStorageWaitNanos", ScanMetricUnit::kNanos},
    {"clpStorageBytesRead", ScanMetricUnit::kBytes},
    {"clpStorageBytesConsumed", ScanMetricUnit::kBytes},
    {"clpStorageCloseNanos", ScanMetricUnit::kNanos},
}};
constexpr size_t kBytesConsumedIndex = 5;

constexpr std::array<std::string_view, 4> kPhaseSuffixes{
    "WallNanos",
    "CpuNanos",
    "StorageWaitNanos",
    "Calls"};
constexpr std::array<ScanMetricUnit, 4> kPhaseUnits{
    ScanMetricUnit::kNanos,
    ScanMetricUnit::kNanos,
    ScanMetricUnit::kNanos,
    ScanMetricUnit::kNone};

const std::array<std::array<std::string, 4>, ClpScanStats::kNumPhases>& phaseMetricNames() {
  static const auto kNames = [] {
    std::array<std::array<std::string, 4>, ClpScanStats::kNumPhases> names;
    for (size_t phase = 0; phase < ClpScanStats::kNumPhases; ++phase) {
      for (size_t value = 0; value < kPhaseSuffixes.size(); ++value) {
        names[phase][value] = std::string("clp")
                                  .append(kPhases[phase].name)
                                  .append(kPhaseSuffixes[value]);
      }
    }
    return names;
  }();
  return kNames;
}

std::array<uint64_t, kStorage.size()> storageValues(const ClpStorageCounters& counters) {
  return {
      counters.readersOpened,
      counters.readersClosed,
      counters.calls,
      counters.waitNanos,
      counters.bytesRead,
      counters.bytesConsumed,
      counters.closeNanos};
}

uint64_t steadyNanos() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

uint64_t threadCpuNanos() {
  struct timespec ts{};
  if (0 != clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts)) {
    return 0;
  }
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

} // namespace

void ScanMetric::add(uint64_t value) {
  auto const v = static_cast<int64_t>(value);
  sum += v;
  ++count;
  min = std::min(min, v);
  max = std::max(max, v);
}

ScanMark ScanMark::now() {
  return ScanMark{steadyNanos(), threadCpuNanos(), ClpStorageCounters::current()};
}

void ClpScanStats::addPhase(ScanPhase phase, const ScanMark& begin, const ScanMark& end) {
  auto const i = static_cast<size_t>(phase);
  auto& values = split_.phases[i];
  values[kWall] += end.wallNanos - begin.wallNanos;
  values[kCpu] += end.cpuNanos - begin.cpuNanos;
  values[kStorageWait] += end.storage.waitNanos - begin.storage.waitNanos;
  ++values[kCalls];

  // Storage work is counted once for the split, from the phases that do not overlap.
  if (Level::kInner != kPhases[i].level) {
    auto const beginValues = storageValues(begin.storage);
    auto const endValues = storageValues(end.storage);
    for (size_t j = 0; j < kStorage.size(); ++j) {
      split_.storage[j] += endValues[j] - beginValues[j];
    }
  }
}

void ClpScanStats::addCounter(ScanCounter counter, uint64_t value) {
  split_.counters[static_cast<size_t>(counter)] += value;
}

void ClpScanStats::beginSplit() {
  split_ = Split{};
  splitOpen_ = true;
  addCounter(ScanCounter::kSplits, 1);
}

void ClpScanStats::finishSplit() {
  if (false == splitOpen_) {
    return;
  }
  fold(split_, totals_);
  bytesConsumedFinished_ += split_.storage[kBytesConsumedIndex];
  split_ = Split{};
  splitOpen_ = false;
}

template <typename F>
void ClpScanStats::forEachObservation(const Split& split, F&& f) {
  auto const& names = phaseMetricNames();
  for (size_t i = 0; i < kNumPhases; ++i) {
    auto const& values = split.phases[i];
    if (0 == values[kCalls]) {
      continue;
    }
    auto const& info = kPhases[i];
    for (size_t value = 0; value < kNumPhaseValues; ++value) {
      if ((kStorageWait == value && false == info.storage) ||
          (kCalls == value && false == info.repeated)) {
        continue;
      }
      f(names[i][value], kPhaseUnits[value], values[value]);
    }
  }
  for (size_t i = 0; i < kNumCounters; ++i) {
    f(kCounters[i].name, kCounters[i].unit, split.counters[i]);
  }
  for (size_t i = 0; i < kStorage.size(); ++i) {
    f(kStorage[i].name, kStorage[i].unit, split.storage[i]);
  }
}

void ClpScanStats::fold(const Split& split, Totals& totals) {
  for (size_t i = 0; i < kNumPhases; ++i) {
    if (0 == split.phases[i][kCalls]) {
      continue;
    }
    for (size_t value = 0; value < kNumPhaseValues; ++value) {
      totals.phases[i][value].add(split.phases[i][value]);
    }
  }
  for (size_t i = 0; i < kNumCounters; ++i) {
    totals.counters[i].add(split.counters[i]);
  }
  for (size_t i = 0; i < kStorage.size(); ++i) {
    totals.storage[i].add(split.storage[i]);
  }
}

std::string ClpScanStats::describeSplit() const {
  std::string json{"{"};
  forEachObservation(split_, [&](std::string_view name, ScanMetricUnit, uint64_t value) {
    if (0 == value) {
      return;
    }
    if (json.size() > 1) {
      json.push_back(',');
    }
    json.append("\"").append(name).append("\":").append(std::to_string(value));
  });
  json.push_back('}');
  return json;
}

std::unordered_map<std::string, ScanMetric> ClpScanStats::metrics() const {
  auto totals = totals_;
  if (splitOpen_) {
    fold(split_, totals);
  }

  std::unordered_map<std::string, ScanMetric> result;
  auto const emit = [&](std::string_view name, ScanMetricUnit unit, const ScanMetric& total) {
    if (0 == total.count) {
      return;
    }
    auto metric = total;
    metric.unit = unit;
    result.emplace(std::string(name), metric);
  };
  auto const& names = phaseMetricNames();
  for (size_t i = 0; i < kNumPhases; ++i) {
    auto const& info = kPhases[i];
    for (size_t value = 0; value < kNumPhaseValues; ++value) {
      if ((kStorageWait == value && false == info.storage) ||
          (kCalls == value && false == info.repeated)) {
        continue;
      }
      emit(names[i][value], kPhaseUnits[value], totals.phases[i][value]);
    }
  }
  for (size_t i = 0; i < kNumCounters; ++i) {
    emit(kCounters[i].name, kCounters[i].unit, totals.counters[i]);
  }
  for (size_t i = 0; i < kStorage.size(); ++i) {
    emit(kStorage[i].name, kStorage[i].unit, totals.storage[i]);
  }
  return result;
}

uint64_t ClpScanStats::bytesConsumed() const {
  return bytesConsumedFinished_ + (splitOpen_ ? split_.storage[kBytesConsumedIndex] : 0);
}

ScanPhaseTimer::ScanPhaseTimer(ClpScanStats* stats, ScanPhase phase)
    : stats_(stats), phase_(phase) {
  if (nullptr != stats_) {
    begin_ = ScanMark::now();
  }
}

ScanPhaseTimer::~ScanPhaseTimer() {
  if (nullptr != stats_) {
    stats_->addPhase(phase_, begin_, ScanMark::now());
  }
}

ScanPhaseLap::ScanPhaseLap(ClpScanStats* stats) : stats_(stats) {
  if (nullptr != stats_) {
    last_ = ScanMark::now();
  }
}

void ScanPhaseLap::mark(ScanPhase phase) {
  if (nullptr == stats_) {
    return;
  }
  auto const now = ScanMark::now();
  stats_->addPhase(phase, last_, now);
  last_ = now;
}

} // namespace facebook::velox::connector::clp::search_lib
