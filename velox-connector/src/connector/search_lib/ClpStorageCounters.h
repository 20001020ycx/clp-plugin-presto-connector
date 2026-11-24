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

/// Running totals of what this thread did through the readers that CLP opens on archives, which
/// `ClpCountingReader` keeps. A scan reads two snapshots of them and takes the difference, so a
/// phase learns how long it waited for storage and how many bytes it pulled without any lookup.
///
/// The counters are per thread because a CLP reader is created, read and destroyed on the thread of
/// the scan that owns it; nothing here needs a lock.
struct ClpStorageCounters {
  /// Readers created, which for archives in object storage is one HTTP GET each.
  uint64_t readersOpened{0};
  /// Readers destroyed.
  uint64_t readersClosed{0};
  /// Calls into the readers (reads and seeks).
  uint64_t calls{0};
  /// Wall time inside those calls: where the scan blocks waiting for data from object storage
  /// (or the disk), plus the copy out of the reader's buffer.
  uint64_t waitNanos{0};
  /// Bytes the reads returned.
  uint64_t bytesRead{0};
  /// How far into the archives the reads and forward seeks went. A network reader fetches every
  /// byte up to that point, so this is the archive data the scan took from storage.
  uint64_t bytesConsumed{0};
  /// Wall time destroying the readers. For a network reader this includes aborting its download
  /// and waiting for the download thread to stop.
  uint64_t closeNanos{0};

  /// @return This thread's counters.
  static ClpStorageCounters& current() {
    thread_local ClpStorageCounters counters;
    return counters;
  }
};

} // namespace facebook::velox::connector::clp::search_lib
