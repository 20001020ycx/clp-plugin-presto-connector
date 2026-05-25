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

#include "clp_s/Defs.hpp"
#include "velox/type/Timestamp.h"

namespace facebook::velox::connector::clp::search_lib {

enum class InputTimestampPrecision : uint8_t {
  Seconds,
  Milliseconds,
  Microseconds,
  Nanoseconds
};

/// Estimates the precision of an epoch timestamp as seconds, milliseconds,
/// microseconds, or nanoseconds.
///
/// This heuristic relies on the fact that 1 year of epoch nanoseconds is
/// approximately 1000 years of epoch microseconds and so on. This heuristic
/// can be unreliable for timestamps sufficiently close to the epoch, but
/// should otherwise be accurate for the next 1000 years.
///
/// Note: Future versions of the clp-s archive format will adopt a
/// nanosecond-precision integer timestamp format (as opposed to the current
/// format which allows other precisions), at which point we can remove this
/// heuristic.
///
/// @param timestamp
/// @return the estimated timestamp precision
template <typename T>
auto estimatePrecision(T timestamp) -> InputTimestampPrecision {
  constexpr int64_t kEpochMilliseconds1971{31536000000};
  constexpr int64_t kEpochMicroseconds1971{31536000000000};
  constexpr int64_t kEpochNanoseconds1971{31536000000000000};
  auto absTimestamp = timestamp >= 0 ? timestamp : -timestamp;

  if (absTimestamp > kEpochNanoseconds1971) {
    return InputTimestampPrecision::Nanoseconds;
  } else if (absTimestamp > kEpochMicroseconds1971) {
    return InputTimestampPrecision::Microseconds;
  } else if (absTimestamp > kEpochMilliseconds1971) {
    return InputTimestampPrecision::Milliseconds;
  } else {
    return InputTimestampPrecision::Seconds;
  }
}

/// Converts a double value into a Velox timestamp.
///
/// @param timestamp the input timestamp as a double
/// @return the corresponding Velox timestamp
inline auto convertToVeloxTimestamp(double timestamp) -> Timestamp {
  switch (estimatePrecision(timestamp)) {
    case InputTimestampPrecision::Nanoseconds:
      timestamp /= Timestamp::kNanosInSecond;
      break;
    case InputTimestampPrecision::Microseconds:
      timestamp /= Timestamp::kMicrosecondsInSecond;
      break;
    case InputTimestampPrecision::Milliseconds:
      timestamp /= Timestamp::kMillisecondsInSecond;
      break;
    case InputTimestampPrecision::Seconds:
      break;
  }
  double seconds{std::floor(timestamp)};
  // Due to IEEE 754 rounding, we drop nanosecond precision to ensure
  // correctness
  double microseconds{(timestamp - seconds) * Timestamp::kMicrosecondsInSecond};
  return Timestamp(
      static_cast<int64_t>(seconds),
      static_cast<int64_t>(std::round(microseconds)) *
          Timestamp::kNanosecondsInMicrosecond);
}

/// Converts an integer value into a Velox timestamp.
///
/// @param timestamp the input timestamp as an integer
/// @return the corresponding Velox timestamp
inline auto convertToVeloxTimestamp(int64_t timestamp) -> Timestamp {
  int64_t precisionDifference{Timestamp::kNanosInSecond};
  switch (estimatePrecision(timestamp)) {
    case InputTimestampPrecision::Nanoseconds:
      break;
    case InputTimestampPrecision::Microseconds:
      precisionDifference =
          Timestamp::kNanosInSecond / Timestamp::kNanosecondsInMicrosecond;
      break;
    case InputTimestampPrecision::Milliseconds:
      precisionDifference =
          Timestamp::kNanosInSecond / Timestamp::kNanosecondsInMillisecond;
      break;
    case InputTimestampPrecision::Seconds:
      precisionDifference =
          Timestamp::kNanosInSecond / Timestamp::kNanosInSecond;
      break;
  }
  int64_t seconds{timestamp / precisionDifference};
  int64_t nanoseconds{
      (timestamp % precisionDifference) *
      (Timestamp::kNanosInSecond / precisionDifference)};
  if (nanoseconds < 0) {
    seconds -= 1;
    nanoseconds += Timestamp::kNanosInSecond;
  }
  return Timestamp(seconds, static_cast<uint64_t>(nanoseconds));
}

/// Converts a nanosecond precision epochtime_t into a Velox timestamp.
///
/// @param timestamp the input timestamp as an integer
/// @return the corresponding Velox timestamp
inline auto convertNanosecondEpochToVeloxTimestamp(clp_s::epochtime_t timestamp)
    -> Timestamp {
  int64_t seconds{timestamp / Timestamp::kNanosInSecond};
  int64_t nanoseconds{timestamp % Timestamp::kNanosInSecond};
  if (nanoseconds < 0) {
    seconds -= 1;
    nanoseconds += Timestamp::kNanosInSecond;
  }
  return Timestamp(seconds, static_cast<uint64_t>(nanoseconds));
}

} // namespace facebook::velox::connector::clp::search_lib
