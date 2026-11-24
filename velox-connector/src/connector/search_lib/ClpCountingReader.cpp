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

// Counts what the scan does through the readers that CLP opens on archives, without changing CLP.
//
// CLP creates each archive's reader through `clp_s::try_create_reader` and uses it only through
// `clp::ReaderInterface` afterwards. The plugin is linked with `--wrap` on that function (see
// velox-connector/CMakeLists.txt), so CLP's call lands in `wrapTryCreateReader` below, which hands
// CLP its own reader wrapped in a `ClpCountingReader`. The wrapper only forwards calls and adds to
// this thread's `ClpStorageCounters`; it changes nothing about what is read.
//
// This file is part of the plugin's shared library itself rather than of a static library, so that
// the linker always has the `__wrap_` definition at hand.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "clp/ErrorCode.hpp"
#include "clp/ReaderInterface.hpp"
#include "clp_s/InputConfig.hpp"
#include "connector/search_lib/ClpStorageCounters.h"

#ifndef CLP_TRY_CREATE_READER_SYMBOL
#error "CLP_TRY_CREATE_READER_SYMBOL must be the mangled name of clp_s::try_create_reader."
#endif

namespace facebook::velox::connector::clp::search_lib {

namespace {

uint64_t steadyNanos() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

/// Forwards every call to CLP's reader and counts it.
class ClpCountingReader final : public ::clp::ReaderInterface {
 public:
  explicit ClpCountingReader(std::shared_ptr<::clp::ReaderInterface> reader)
      : reader_(std::move(reader)) {
    if (::clp::ErrorCode_Success != reader_->try_get_pos(position_)) {
      position_ = 0;
    }
    furthest_ = position_;
    ++ClpStorageCounters::current().readersOpened;
  }

  ~ClpCountingReader() override {
    auto const begin = steadyNanos();
    reader_.reset();
    auto& counters = ClpStorageCounters::current();
    counters.closeNanos += steadyNanos() - begin;
    ++counters.readersClosed;
  }

  ClpCountingReader(const ClpCountingReader&) = delete;
  ClpCountingReader& operator=(const ClpCountingReader&) = delete;

  ::clp::ErrorCode try_read(char* buf, size_t numBytesToRead, size_t& numBytesRead) override {
    auto const begin = steadyNanos();
    auto const errorCode = reader_->try_read(buf, numBytesToRead, numBytesRead);
    auto& counters = endCall(begin);
    if (::clp::ErrorCode_Success == errorCode || ::clp::ErrorCode_EndOfFile == errorCode) {
      counters.bytesRead += numBytesRead;
      moveTo(position_ + numBytesRead, counters);
    }
    return errorCode;
  }

  ::clp::ErrorCode try_seek_from_begin(size_t pos) override {
    auto const begin = steadyNanos();
    auto const errorCode = reader_->try_seek_from_begin(pos);
    auto& counters = endCall(begin);
    if (::clp::ErrorCode_Success == errorCode) {
      moveTo(pos, counters);
    }
    return errorCode;
  }

  ::clp::ErrorCode try_get_pos(size_t& pos) override {
    return reader_->try_get_pos(pos);
  }

  ::clp::ErrorCode try_read_to_delimiter(
      char delim,
      bool keepDelimiter,
      bool append,
      std::string& str) override {
    auto const sizeBefore = append ? str.size() : 0;
    auto const begin = steadyNanos();
    auto const errorCode = reader_->try_read_to_delimiter(delim, keepDelimiter, append, str);
    auto& counters = endCall(begin);
    if (::clp::ErrorCode_Success == errorCode || ::clp::ErrorCode_EndOfFile == errorCode) {
      size_t pos{0};
      if (::clp::ErrorCode_Success == reader_->try_get_pos(pos)) {
        counters.bytesRead += str.size() - sizeBefore;
        moveTo(pos, counters);
      }
    }
    return errorCode;
  }

 private:
  static ClpStorageCounters& endCall(uint64_t beginNanos) {
    auto& counters = ClpStorageCounters::current();
    counters.waitNanos += steadyNanos() - beginNanos;
    ++counters.calls;
    return counters;
  }

  /// Moves the position and counts the bytes beyond the furthest point reached so far, which the
  /// reader has had to take from storage.
  void moveTo(size_t pos, ClpStorageCounters& counters) {
    position_ = pos;
    if (position_ > furthest_) {
      counters.bytesConsumed += position_ - furthest_;
      furthest_ = position_;
    }
  }

  std::shared_ptr<::clp::ReaderInterface> reader_;
  size_t position_{0};
  size_t furthest_{0};
};

} // namespace

} // namespace facebook::velox::connector::clp::search_lib

// With `--wrap=<symbol>`, the linker sends CLP's calls of `<symbol>` to `__wrap_<symbol>` and
// resolves `__real_<symbol>` to CLP's definition. If the symbol's name ever stops matching CLP's
// declaration, `__real_<symbol>` is left undefined and the link fails.
std::shared_ptr<clp::ReaderInterface> realTryCreateReader(
    clp_s::Path const& path,
    clp_s::NetworkAuthOption const& networkAuth) __asm__("__real_" CLP_TRY_CREATE_READER_SYMBOL);

std::shared_ptr<clp::ReaderInterface> wrapTryCreateReader(
    clp_s::Path const& path,
    clp_s::NetworkAuthOption const& networkAuth) __asm__("__wrap_" CLP_TRY_CREATE_READER_SYMBOL);

std::shared_ptr<clp::ReaderInterface> wrapTryCreateReader(
    clp_s::Path const& path,
    clp_s::NetworkAuthOption const& networkAuth) {
  auto reader = realTryCreateReader(path, networkAuth);
  if (nullptr == reader) {
    return reader;
  }
  return std::make_shared<facebook::velox::connector::clp::search_lib::ClpCountingReader>(
      std::move(reader));
}
