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

#include "connector/search_lib/ClpTimestampPrecision.h"

#include <cstdint>

#include <gtest/gtest.h>

namespace facebook::velox::connector::clp::search_lib {
namespace {

// 2025-04-30T08:50:05Z, the instant that the timestamp fixtures store four ways.
constexpr int64_t kSeconds{1746003005};
constexpr int64_t kMilliseconds{1746003005000};
constexpr int64_t kMicroseconds{1746003005000000};
constexpr int64_t kNanoseconds{1746003005000000000};

// The thresholds that the heuristic switches on: one year of epoch time in each unit.
constexpr int64_t kMillisecondsThreshold{31536000000};
constexpr int64_t kMicrosecondsThreshold{31536000000000};
constexpr int64_t kNanosecondsThreshold{31536000000000000};

TEST(ClpTimestampPrecisionTest, classifiesEachUnit) {
  EXPECT_EQ(estimatePrecision(kSeconds), InputTimestampPrecision::Seconds);
  EXPECT_EQ(estimatePrecision(kMilliseconds), InputTimestampPrecision::Milliseconds);
  EXPECT_EQ(estimatePrecision(kMicroseconds), InputTimestampPrecision::Microseconds);
  EXPECT_EQ(estimatePrecision(kNanoseconds), InputTimestampPrecision::Nanoseconds);
}

// The comparisons are strictly greater-than, so a value exactly on a threshold reads as the
// coarser unit.
TEST(ClpTimestampPrecisionTest, thresholdItselfReadsAsTheCoarserUnit) {
  EXPECT_EQ(estimatePrecision(kMillisecondsThreshold), InputTimestampPrecision::Seconds);
  EXPECT_EQ(estimatePrecision(kMillisecondsThreshold + 1), InputTimestampPrecision::Milliseconds);
  EXPECT_EQ(estimatePrecision(kMicrosecondsThreshold), InputTimestampPrecision::Milliseconds);
  EXPECT_EQ(estimatePrecision(kMicrosecondsThreshold + 1), InputTimestampPrecision::Microseconds);
  EXPECT_EQ(estimatePrecision(kNanosecondsThreshold), InputTimestampPrecision::Microseconds);
  EXPECT_EQ(estimatePrecision(kNanosecondsThreshold + 1), InputTimestampPrecision::Nanoseconds);
}

TEST(ClpTimestampPrecisionTest, classifiesByMagnitudeSoNegativesMatch) {
  EXPECT_EQ(estimatePrecision(-kMilliseconds), InputTimestampPrecision::Milliseconds);
  EXPECT_EQ(estimatePrecision(-kNanoseconds), InputTimestampPrecision::Nanoseconds);
}

// Anything below a year past the epoch is indistinguishable from seconds, which is the documented
// blind spot of the heuristic.
TEST(ClpTimestampPrecisionTest, valuesNearTheEpochReadAsSeconds) {
  EXPECT_EQ(estimatePrecision(int64_t{0}), InputTimestampPrecision::Seconds);
  EXPECT_EQ(estimatePrecision(int64_t{1}), InputTimestampPrecision::Seconds);
  EXPECT_EQ(estimatePrecision(int64_t{-1}), InputTimestampPrecision::Seconds);
}

TEST(ClpTimestampPrecisionTest, acceptsDoubleTimestamps) {
  EXPECT_EQ(estimatePrecision(1746003005.5), InputTimestampPrecision::Seconds);
  EXPECT_EQ(estimatePrecision(1746003005000.5), InputTimestampPrecision::Milliseconds);
}

} // namespace
} // namespace facebook::velox::connector::clp::search_lib
