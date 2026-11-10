/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
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

#pragma once

// thesis-metrics: timers and counters of one ClpDataSource, that is, of one
// driver's scan. Measurement only: nothing in the connector reads these values
// to make a decision.
//
// A data source is used by one thread at a time, so the values are plain
// integers. Values are kept per split and folded into running totals when the
// split's cursor has been destroyed; one line per finished split goes to the
// worker's log.

#include <glog/logging.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "clp/ThesisNetworkCounters.hpp"
#include "velox/common/base/RuntimeMetrics.h"

namespace facebook::velox::connector::clp::search_lib {

/// The timed phases. kAddSplit, kCloseArchive and kNext are disjoint and
/// together cover the data source's calls from TableScan; the phases from
/// kParseQuery to kCreateVector lie inside them (kParseQuery inside kAddSplit,
/// the others inside kNext, and those from kOpenArchive to kQueryRunnerInit
/// inside kLoadSplit). The two lazy-load phases run when an operator downstream
/// reads a column, outside the three.
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
  kSchemasInArchive = 0,
  kSchemasMatched,
  kTablesRead,
  kRowsScanned,
  kRowsMatched,
  kLazyJsonRows,
  kLazyJsonBytes,
  kLazyColumnRows,
  kNumCounters
};

class ClpScanStats {
 public:
  static constexpr size_t kNumPhases =
      static_cast<size_t>(ScanPhase::kNumPhases);
  static constexpr size_t kNumCounters =
      static_cast<size_t>(ScanCounter::kNumCounters);

  /// A reading of this thread's clocks and of CLP's network counters.
  struct Mark {
    std::chrono::steady_clock::time_point wall;
    uint64_t cpuNanos{0};
    ::clp::ThesisNetworkCounters network;
  };

  explicit ClpScanStats(std::string label) : label_(std::move(label)) {}

  static uint64_t threadCpuNanos() {
    struct timespec ts {};
    if (0 != clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts)) {
      return 0;
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
        static_cast<uint64_t>(ts.tv_nsec);
  }

  static uint64_t networkWaitNanos() {
    return ::clp::thesis_network_counters().wait_ns;
  }

  static Mark mark() {
    return Mark{
        std::chrono::steady_clock::now(),
        threadCpuNanos(),
        ::clp::thesis_network_counters()};
  }

  static uint64_t nanosBetween(
      std::chrono::steady_clock::time_point begin,
      std::chrono::steady_clock::time_point end) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
            .count());
  }

  /// Adds wall time (and the part of it spent waiting for the network) of a
  /// phase that lies inside one of the outer phases.
  void addPhase(ScanPhase phase, uint64_t wallNanos, uint64_t networkWaitNanos) {
    auto const i = static_cast<size_t>(phase);
    split_.wallNanos[i] += wallNanos;
    split_.networkWaitNanos[i] += networkWaitNanos;
    ++split_.calls[i];
  }

  /// Adds a lazy load: wall and CPU time of one loader call.
  void addLazyLoad(ScanPhase phase, uint64_t wallNanos, uint64_t cpuNanos) {
    auto const i = static_cast<size_t>(phase);
    split_.wallNanos[i] += wallNanos;
    split_.cpuNanos[i] += cpuNanos;
    ++split_.calls[i];
  }

  /// Adds the interval between two marks made on the same thread to one of the
  /// outer phases (kAddSplit, kCloseArchive, kNext): wall time, CPU time, and
  /// everything the network readers did in it.
  void addOuter(
      ScanPhase phase,
      const Mark& begin,
      const Mark& end,
      bool countCall = true) {
    auto const i = static_cast<size_t>(phase);
    split_.wallNanos[i] += nanosBetween(begin.wall, end.wall);
    split_.cpuNanos[i] += end.cpuNanos - begin.cpuNanos;
    auto const waitNanos = end.network.wait_ns - begin.network.wait_ns;
    split_.networkWaitNanos[i] += waitNanos;
    if (countCall) {
      ++split_.calls[i];
    }
    auto const bytesConsumed =
        end.network.bytes_consumed - begin.network.bytes_consumed;
    split_.network.wait_ns += waitNanos;
    split_.network.wait_count +=
        end.network.wait_count - begin.network.wait_count;
    split_.network.bytes_consumed += bytesConsumed;
    split_.network.requests += end.network.requests - begin.network.requests;
    split_.network.readers_closed +=
        end.network.readers_closed - begin.network.readers_closed;
    split_.network.bytes_downloaded +=
        end.network.bytes_downloaded - begin.network.bytes_downloaded;
    split_.network.close_wait_ns +=
        end.network.close_wait_ns - begin.network.close_wait_ns;
    split_.network.downloader_cpu_ns +=
        end.network.downloader_cpu_ns - begin.network.downloader_cpu_ns;
    split_.network.downloader_wall_ns +=
        end.network.downloader_wall_ns - begin.network.downloader_wall_ns;
    bytesConsumedTotal_ += bytesConsumed;
  }

  void addCounter(ScanCounter counter, uint64_t value) {
    split_.counters[static_cast<size_t>(counter)] += value;
  }

  bool splitOpen() const {
    return splitOpen_;
  }

  void beginSplit(std::string path) {
    splitPath_ = std::move(path);
    splitOpen_ = true;
  }

  /// Folds the split's values into the totals and writes its log line. Called
  /// when the split's cursor has been destroyed.
  void finishSplit() {
    if (!splitOpen_) {
      return;
    }
    ++splitsFinished_;
    std::ostringstream line;
    line << "thesis-metrics split {\"scan\":\"" << label_
         << "\",\"seq\":" << splitsFinished_ << ",\"path\":\"" << splitPath_
         << "\",\"m\":{";
    bool first = true;
    forEachMetric(
        split_,
        [&](const std::string& name, RuntimeCounter::Unit unit, uint64_t v) {
          auto& total = totals_.try_emplace(name, Total{unit}).first->second;
          total.add(v);
          line << (first ? "\"" : ",\"") << name << "\":" << v;
          first = false;
        });
    line << "}}";
    LOG(INFO) << line.str();
    split_ = Split{};
    splitOpen_ = false;
  }

  /// The totals of the finished splits, with the split in progress (if any) as
  /// one more observation. Sum is the total over splits; count, min and max
  /// describe the per-split values.
  std::unordered_map<std::string, RuntimeMetric> runtimeStats() const {
    auto totals = totals_;
    if (splitOpen_) {
      forEachMetric(
          split_,
          [&](const std::string& name, RuntimeCounter::Unit unit, uint64_t v) {
            totals.try_emplace(name, Total{unit}).first->second.add(v);
          });
    }
    std::unordered_map<std::string, RuntimeMetric> result;
    result.reserve(totals.size());
    for (auto const& [name, total] : totals) {
      result.emplace(
          name,
          RuntimeMetric(
              total.sum, total.count, total.min, total.max, total.unit));
    }
    return result;
  }

  /// Bytes the scan's thread took from the network readers so far.
  uint64_t bytesConsumedTotal() const {
    return bytesConsumedTotal_;
  }

 private:
  struct Split {
    std::array<uint64_t, kNumPhases> wallNanos{};
    std::array<uint64_t, kNumPhases> networkWaitNanos{};
    std::array<uint64_t, kNumPhases> cpuNanos{};
    std::array<uint64_t, kNumPhases> calls{};
    std::array<uint64_t, kNumCounters> counters{};
    ::clp::ThesisNetworkCounters network;
  };

  struct Total {
    RuntimeCounter::Unit unit;
    int64_t sum{0};
    uint64_t count{0};
    int64_t min{std::numeric_limits<int64_t>::max()};
    int64_t max{std::numeric_limits<int64_t>::min()};

    void add(uint64_t value) {
      auto const v = static_cast<int64_t>(value);
      sum += v;
      ++count;
      min = std::min(min, v);
      max = std::max(max, v);
    }
  };

  struct PhaseInfo {
    const char* name;
    // Whether the phase can read from the network.
    bool network;
    // Whether thread CPU time is measured for it.
    bool cpu;
    // Whether it runs more than once per split.
    bool repeated;
  };

  static const PhaseInfo& phaseInfo(size_t phase) {
    static const std::array<PhaseInfo, kNumPhases> kInfo{{
        {"clpAddSplit", false, true, false},
        {"clpCloseArchive", true, true, false},
        {"clpNext", true, true, true},
        {"clpParseQuery", false, false, false},
        {"clpLoadSplit", true, false, false},
        {"clpOpenArchive", true, false, false},
        {"clpTimestampIndexAndSchemaMatch", false, false, false},
        {"clpProjection", false, false, false},
        {"clpReadTableMetadata", true, false, false},
        {"clpLoadDictionaries", true, false, false},
        {"clpOpenPackedStreams", true, false, false},
        {"clpQueryRunnerInit", false, false, false},
        {"clpSchemaInit", false, false, true},
        {"clpReadTable", true, false, true},
        {"clpFilterInit", false, false, true},
        {"clpFilter", false, false, true},
        {"clpCreateVector", false, false, true},
        {"clpLazyLoadJson", false, true, true},
        {"clpLazyLoadColumn", false, true, true},
    }};
    return kInfo[phase];
  }

  template <typename F>
  static void forEachMetric(const Split& s, F&& f) {
    using Unit = RuntimeCounter::Unit;
    for (size_t i = 0; i < kNumPhases; ++i) {
      if (0 == s.calls[i]) {
        continue;
      }
      auto const& info = phaseInfo(i);
      const std::string name{info.name};
      f(name + "WallNanos", Unit::kNanos, s.wallNanos[i]);
      if (info.network || 0 != s.networkWaitNanos[i]) {
        f(name + "NetworkWaitNanos", Unit::kNanos, s.networkWaitNanos[i]);
      }
      if (info.cpu) {
        f(name + "CpuNanos", Unit::kNanos, s.cpuNanos[i]);
      }
      if (info.repeated) {
        f(name + "Calls", Unit::kNone, s.calls[i]);
      }
    }
    static const std::array<const char*, kNumCounters> kCounterNames{{
        "clpSchemasInArchive",
        "clpSchemasMatched",
        "clpTablesRead",
        "clpRowsScanned",
        "clpRowsMatched",
        "clpLazyJsonRows",
        "clpLazyJsonBytes",
        "clpLazyColumnRows",
    }};
    for (size_t i = 0; i < kNumCounters; ++i) {
      f(kCounterNames[i],
        static_cast<size_t>(ScanCounter::kLazyJsonBytes) == i ? Unit::kBytes
                                                              : Unit::kNone,
        s.counters[i]);
    }
    f("clpNetworkWaitNanos", Unit::kNanos, s.network.wait_ns);
    f("clpNetworkWaits", Unit::kNone, s.network.wait_count);
    f("clpNetworkBytesConsumed", Unit::kBytes, s.network.bytes_consumed);
    f("clpNetworkRequests", Unit::kNone, s.network.requests);
    f("clpNetworkReadersClosed", Unit::kNone, s.network.readers_closed);
    f("clpNetworkBytesDownloaded", Unit::kBytes, s.network.bytes_downloaded);
    f("clpNetworkCloseWaitNanos", Unit::kNanos, s.network.close_wait_ns);
    f("clpDownloaderThreadCpuNanos", Unit::kNanos, s.network.downloader_cpu_ns);
    f("clpDownloaderThreadWallNanos",
      Unit::kNanos,
      s.network.downloader_wall_ns);
  }

  const std::string label_;
  std::string splitPath_;
  bool splitOpen_{false};
  uint64_t splitsFinished_{0};
  uint64_t bytesConsumedTotal_{0};
  Split split_;
  std::unordered_map<std::string, Total> totals_;
};

/// Times consecutive pieces of a function: each mark() adds the time since the
/// previous mark (or since construction) to a phase. Does nothing when there
/// are no stats.
class PhaseLap {
 public:
  explicit PhaseLap(ClpScanStats* stats) : stats_(stats) {
    if (stats_ != nullptr) {
      wall_ = std::chrono::steady_clock::now();
      networkWaitNanos_ = ClpScanStats::networkWaitNanos();
    }
  }

  void mark(ScanPhase phase) {
    if (stats_ == nullptr) {
      return;
    }
    auto const now = std::chrono::steady_clock::now();
    auto const networkWaitNanos = ClpScanStats::networkWaitNanos();
    stats_->addPhase(
        phase,
        ClpScanStats::nanosBetween(wall_, now),
        networkWaitNanos - networkWaitNanos_);
    wall_ = now;
    networkWaitNanos_ = networkWaitNanos;
  }

 private:
  ClpScanStats* const stats_;
  std::chrono::steady_clock::time_point wall_;
  uint64_t networkWaitNanos_{0};
};

/// Times a lazy load (wall and thread CPU) from construction to destruction.
class LazyLoadTimer {
 public:
  LazyLoadTimer(ClpScanStats* stats, ScanPhase phase)
      : stats_(stats), phase_(phase) {
    if (stats_ != nullptr) {
      cpuNanos_ = ClpScanStats::threadCpuNanos();
      wall_ = std::chrono::steady_clock::now();
    }
  }

  ~LazyLoadTimer() {
    if (stats_ != nullptr) {
      auto const wallNanos = ClpScanStats::nanosBetween(
          wall_, std::chrono::steady_clock::now());
      stats_->addLazyLoad(
          phase_, wallNanos, ClpScanStats::threadCpuNanos() - cpuNanos_);
    }
  }

 private:
  ClpScanStats* const stats_;
  const ScanPhase phase_;
  std::chrono::steady_clock::time_point wall_;
  uint64_t cpuNanos_{0};
};

} // namespace facebook::velox::connector::clp::search_lib
