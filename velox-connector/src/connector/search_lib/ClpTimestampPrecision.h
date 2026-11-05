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

#include <cstdint>

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

} // namespace facebook::velox::connector::clp::search_lib
