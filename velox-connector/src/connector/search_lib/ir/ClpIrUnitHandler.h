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

#include <memory>
#include <vector>

#include "clp_s/SchemaTree.hpp"
#include "ffi/ir_stream/Deserializer.hpp"
#include "velox/common/base/Exceptions.h"
#include "connector/search_lib/ir/ClpIrUnitHandler.h"

namespace facebook::velox::connector::clp::search_lib {

class ClpIrUnitHandler {
 public:
  ClpIrUnitHandler(
      std::shared_ptr<
          std::vector<std::unique_ptr<::clp::ffi::KeyValuePairLogEvent>>>
          filteredLogEvents)
      : filteredLogEvents_(filteredLogEvents) {
    VELOX_CHECK_NOT_NULL(
        filteredLogEvents_, "filteredLogEvents cannot be null");
  }

  // Destructor
  ~ClpIrUnitHandler() = default;

  // Methods implementing `IrUnitHandlerInterface`
  [[nodiscard]] auto handle_log_event(
      ::clp::ffi::KeyValuePairLogEvent log_event,
      size_t log_event_idx) -> ::clp::ffi::ir_stream::IRErrorCode {
    filteredLogEvents_->push_back(
        std::make_unique<::clp::ffi::KeyValuePairLogEvent>(
            std::move(log_event)));
    return ::clp::ffi::ir_stream::IRErrorCode::IRErrorCode_Success;
  }

  [[nodiscard]] auto handle_utc_offset_change(
      [[maybe_unused]] ::clp::UtcOffset utc_offset_old,
      [[maybe_unused]] ::clp::UtcOffset utc_offset_new)
      -> ::clp::ffi::ir_stream::IRErrorCode {
    return ::clp::ffi::ir_stream::IRErrorCode::IRErrorCode_Success;
  }

  [[nodiscard]] auto handle_schema_tree_node_insertion(
      [[maybe_unused]] bool is_auto_generated,
      [[maybe_unused]] ::clp::ffi::SchemaTree::NodeLocator
          schema_tree_node_locator,
      [[maybe_unused]] std::shared_ptr<::clp::ffi::SchemaTree const> const&
          schema_tree) -> ::clp::ffi::ir_stream::IRErrorCode {
    return ::clp::ffi::ir_stream::IRErrorCode::IRErrorCode_Success;
  }

  [[nodiscard]] auto handle_end_of_stream()
      -> ::clp::ffi::ir_stream::IRErrorCode {
    return ::clp::ffi::ir_stream::IRErrorCode::IRErrorCode_Success;
  }

 private:
  std::shared_ptr<
      std::vector<std::unique_ptr<::clp::ffi::KeyValuePairLogEvent>>>
      filteredLogEvents_;
};

} // namespace facebook::velox::connector::clp::search_lib
