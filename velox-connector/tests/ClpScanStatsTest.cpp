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

#include "connector/search_lib/ClpScanStats.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "connector/search_lib/ClpStorageCounters.h"

namespace facebook::velox::connector::clp::search_lib {
namespace {

ScanMark markAt(uint64_t wallNanos, uint64_t cpuNanos = 0, uint64_t bytesConsumed = 0) {
  ScanMark mark;
  mark.wallNanos = wallNanos;
  mark.cpuNanos = cpuNanos;
  mark.storage.bytesConsumed = bytesConsumed;
  mark.storage.waitNanos = bytesConsumed;
  return mark;
}

const ScanMetric& at(
    const std::unordered_map<std::string, ScanMetric>& metrics,
    const std::string& name) {
  auto const it = metrics.find(name);
  if (metrics.end() == it) {
    ADD_FAILURE() << "missing metric " << name;
    static const ScanMetric kMissing;
    return kMissing;
  }
  return it->second;
}

/// Burns CPU for about `micros` microseconds, so that CPU time advances along with wall time.
void spin(int micros) {
  auto const until = std::chrono::steady_clock::now() + std::chrono::microseconds(micros);
  volatile uint64_t sink{0};
  while (std::chrono::steady_clock::now() < until) {
    sink = sink + 1;
  }
}

TEST(ClpScanStatsTest, reportsNothingBeforeTheFirstSplit) {
  ClpScanStats stats;
  EXPECT_TRUE(stats.metrics().empty());
  EXPECT_EQ(stats.bytesConsumed(), 0);
}

TEST(ClpScanStatsTest, foldsSplitsIntoSumCountMinAndMax) {
  ClpScanStats stats;
  stats.beginSplit();
  stats.addPhase(ScanPhase::kAddSplit, markAt(0, 0), markAt(100, 40));
  stats.addPhase(ScanPhase::kNext, markAt(100), markAt(1'100));
  stats.addPhase(ScanPhase::kNext, markAt(1'100), markAt(1'200));
  stats.finishSplit();
  stats.beginSplit();
  stats.addPhase(ScanPhase::kAddSplit, markAt(0, 0), markAt(300, 60));
  stats.finishSplit();

  auto const metrics = stats.metrics();
  auto const& addSplit = at(metrics, "clpAddSplitWallNanos");
  EXPECT_EQ(addSplit.unit, ScanMetricUnit::kNanos);
  EXPECT_EQ(addSplit.sum, 400);
  EXPECT_EQ(addSplit.count, 2);
  EXPECT_EQ(addSplit.min, 100);
  EXPECT_EQ(addSplit.max, 300);
  EXPECT_EQ(at(metrics, "clpAddSplitCpuNanos").sum, 100);

  // A phase is an observation only of the splits in which it ran.
  auto const& next = at(metrics, "clpNextWallNanos");
  EXPECT_EQ(next.sum, 1'100);
  EXPECT_EQ(next.count, 1);
  EXPECT_EQ(at(metrics, "clpNextCalls").sum, 2);

  auto const& splits = at(metrics, "clpSplits");
  EXPECT_EQ(splits.unit, ScanMetricUnit::kNone);
  EXPECT_EQ(splits.sum, 2);
  EXPECT_EQ(splits.count, 2);
}

TEST(ClpScanStatsTest, countsTheSplitInProgressAsOneMoreObservation) {
  ClpScanStats stats;
  stats.beginSplit();
  stats.addPhase(ScanPhase::kNext, markAt(0), markAt(10));
  stats.finishSplit();
  stats.beginSplit();
  stats.addPhase(ScanPhase::kNext, markAt(0, 0, 0), markAt(30, 0, 700));
  ASSERT_TRUE(stats.splitOpen());

  auto const metrics = stats.metrics();
  EXPECT_EQ(at(metrics, "clpNextWallNanos").sum, 40);
  EXPECT_EQ(at(metrics, "clpNextWallNanos").count, 2);
  EXPECT_EQ(at(metrics, "clpSplits").sum, 2);
  EXPECT_EQ(stats.bytesConsumed(), 700);

  // Reading the metrics does not fold the split.
  EXPECT_TRUE(stats.splitOpen());
  EXPECT_EQ(at(stats.metrics(), "clpSplits").sum, 2);
}

TEST(ClpScanStatsTest, reportsCallsAndStorageWaitOnlyWhereTheyApply) {
  ClpScanStats stats;
  stats.beginSplit();
  stats.addPhase(ScanPhase::kAddSplit, markAt(0), markAt(1));
  stats.addPhase(ScanPhase::kParseQuery, markAt(0), markAt(1));
  stats.addPhase(ScanPhase::kNext, markAt(1), markAt(2));
  stats.addPhase(ScanPhase::kReadTable, markAt(1), markAt(2));
  stats.finishSplit();

  auto const metrics = stats.metrics();
  EXPECT_TRUE(metrics.contains("clpNextCalls"));
  EXPECT_TRUE(metrics.contains("clpReadTableCalls"));
  EXPECT_FALSE(metrics.contains("clpAddSplitCalls"));
  EXPECT_FALSE(metrics.contains("clpParseQueryCalls"));

  EXPECT_TRUE(metrics.contains("clpNextStorageWaitNanos"));
  EXPECT_TRUE(metrics.contains("clpReadTableStorageWaitNanos"));
  EXPECT_FALSE(metrics.contains("clpAddSplitStorageWaitNanos"));
  EXPECT_FALSE(metrics.contains("clpParseQueryStorageWaitNanos"));

  // A phase that never ran is absent.
  EXPECT_FALSE(metrics.contains("clpLoadSplitWallNanos"));
}

TEST(ClpScanStatsTest, countsStorageOnceForTheSplit) {
  ClpScanStats stats;
  stats.beginSplit();
  // An inner phase sees part of what its outer phase sees; the split counts the outer one only.
  stats.addPhase(ScanPhase::kOpenArchive, markAt(10, 0, 100), markAt(20, 0, 700));
  stats.addPhase(ScanPhase::kLoadSplit, markAt(5, 0, 100), markAt(25, 0, 900));
  stats.addPhase(ScanPhase::kNext, markAt(0, 0, 0), markAt(30, 0, 1'000));
  stats.addPhase(ScanPhase::kLazyLoadColumn, markAt(40, 0, 1'000), markAt(50, 0, 1'024));
  stats.finishSplit();

  auto const metrics = stats.metrics();
  EXPECT_EQ(at(metrics, "clpStorageBytesConsumed").sum, 1'024);
  EXPECT_EQ(at(metrics, "clpStorageBytesConsumed").unit, ScanMetricUnit::kBytes);
  EXPECT_EQ(at(metrics, "clpStorageWaitNanos").sum, 1'024);
  EXPECT_EQ(at(metrics, "clpOpenArchiveStorageWaitNanos").sum, 600);
  EXPECT_EQ(at(metrics, "clpLoadSplitStorageWaitNanos").sum, 800);
  EXPECT_EQ(stats.bytesConsumed(), 1'024);
}

TEST(ClpScanStatsTest, nestedPhasesOnRealClocksStayInsideTheirParents) {
  ClpScanStats stats;
  auto& storage = ClpStorageCounters::current();
  for (int split = 0; split < 3; ++split) {
    stats.beginSplit();
    {
      ScanPhaseTimer addSplit(&stats, ScanPhase::kAddSplit);
      ScanPhaseTimer parse(&stats, ScanPhase::kParseQuery);
      spin(200);
    }
    {
      ScanPhaseTimer next(&stats, ScanPhase::kNext);
      {
        ScanPhaseTimer load(&stats, ScanPhase::kLoadSplit);
        ScanPhaseLap lap(&stats);
        spin(300);
        storage.bytesConsumed += 4'096;
        storage.waitNanos += 1'000;
        lap.mark(ScanPhase::kOpenArchive);
        spin(100);
        lap.mark(ScanPhase::kLoadDictionaries);
      }
      ScanPhaseLap lap(&stats);
      spin(200);
      lap.mark(ScanPhase::kReadTable);
      spin(200);
      lap.mark(ScanPhase::kFilter);
    }
    {
      ScanPhaseTimer close(&stats, ScanPhase::kCloseArchive);
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    stats.finishSplit();
  }

  auto const metrics = stats.metrics();
  auto const sum = [&](const std::string& name) { return at(metrics, name).sum; };
  for (std::string const value : {"WallNanos", "CpuNanos"}) {
    EXPECT_LE(sum("clpParseQuery" + value), sum("clpAddSplit" + value)) << value;
    EXPECT_LE(
        sum("clpLoadSplit" + value) + sum("clpReadTable" + value) + sum("clpFilter" + value),
        sum("clpNext" + value))
        << value;
    EXPECT_LE(
        sum("clpOpenArchive" + value) + sum("clpLoadDictionaries" + value),
        sum("clpLoadSplit" + value))
        << value;
  }
  EXPECT_LE(sum("clpOpenArchiveStorageWaitNanos"), sum("clpLoadSplitStorageWaitNanos"));
  EXPECT_LE(sum("clpLoadSplitStorageWaitNanos"), sum("clpNextStorageWaitNanos"));
  EXPECT_EQ(sum("clpStorageBytesConsumed"), 3 * 4'096);
  EXPECT_EQ(stats.bytesConsumed(), 3 * 4'096);

  // Sleeping is wall time without CPU time.
  EXPECT_GE(sum("clpCloseArchiveWallNanos"), 3 * 200'000);
  EXPECT_LT(sum("clpCloseArchiveCpuNanos"), sum("clpCloseArchiveWallNanos"));
  EXPECT_EQ(at(metrics, "clpSplits").sum, 3);
}

TEST(ClpScanStatsTest, timersWithoutStatsDoNothing) {
  ScanPhaseTimer timer(nullptr, ScanPhase::kNext);
  ScanPhaseLap lap(nullptr);
  lap.mark(ScanPhase::kFilter);
}

TEST(ClpScanStatsTest, describesTheSplitInProgress) {
  ClpScanStats stats;
  stats.beginSplit();
  stats.addPhase(ScanPhase::kNext, markAt(0, 0, 0), markAt(30, 10, 512));
  stats.addCounter(ScanCounter::kRowsMatched, 7);
  auto const json = stats.describeSplit();
  EXPECT_EQ(json.front(), '{');
  EXPECT_EQ(json.back(), '}');
  EXPECT_NE(json.find("\"clpSplits\":1"), std::string::npos);
  EXPECT_NE(json.find("\"clpNextWallNanos\":30"), std::string::npos);
  EXPECT_NE(json.find("\"clpRowsMatched\":7"), std::string::npos);
  EXPECT_NE(json.find("\"clpStorageBytesConsumed\":512"), std::string::npos);
  // Zero values are left out.
  EXPECT_EQ(json.find("clpRowsScanned"), std::string::npos);
}

TEST(ClpScanStatsTest, everyMetricIsInTheGlossary) {
  ClpScanStats stats;
  stats.beginSplit();
  for (size_t phase = 0; phase < ClpScanStats::kNumPhases; ++phase) {
    stats.addPhase(static_cast<ScanPhase>(phase), markAt(0), markAt(1));
  }
  stats.finishSplit();

  std::ifstream file(CLP_RUNTIME_METRICS_GLOSSARY);
  ASSERT_TRUE(file.good()) << CLP_RUNTIME_METRICS_GLOSSARY;
  std::stringstream glossary;
  glossary << file.rdbuf();

  auto const metrics = stats.metrics();
  EXPECT_GT(metrics.size(), 60);
  for (auto const& [name, metric] : metrics) {
    EXPECT_EQ(name.rfind("clp", 0), 0) << name;
    EXPECT_NE(glossary.str().find("`" + name + "`"), std::string::npos)
        << name << " is not in " << CLP_RUNTIME_METRICS_GLOSSARY;
  }
}

} // namespace
} // namespace facebook::velox::connector::clp::search_lib
