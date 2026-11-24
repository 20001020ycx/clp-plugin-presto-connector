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

#include <set>
#include <string>

#include "velox/connectors/Connector.h"
#include "connector/ClpConfig.h"
#include "connector/search_lib/BaseClpCursor.h"
#include "connector/search_lib/ClpScanStats.h"

namespace clp_s {
class BaseColumnReader;
} // namespace clp_s

namespace facebook::velox::connector::clp {

class ClpS3AuthProviderBase;

class ClpDataSource : public DataSource {
 public:
  /// @param outputType
  /// @param tableHandle
  /// @param columnHandles
  /// @param pool
  /// @param clpConfig
  /// @param scanLabel Names the scan in the per-split log lines (see
  /// `ClpConfig::kLogSplitStats`).
  ClpDataSource(
      const RowTypePtr& outputType,
      const ConnectorTableHandlePtr& tableHandle,
      const connector::ColumnHandleMap& columnHandles,
      velox::memory::MemoryPool* pool,
      std::shared_ptr<const ClpConfig>& clpConfig,
      std::string scanLabel = "");

  ~ClpDataSource() override;

  void addSplit(std::shared_ptr<ConnectorSplit> split) override;

  std::optional<RowVectorPtr> next(uint64_t size, velox::ContinueFuture& future)
      override;

  void addDynamicFilter(
      column_index_t outputChannel,
      const std::shared_ptr<common::Filter>& filter) override {
    VELOX_NYI("Dynamic filters not supported by ClpConnector.");
  }

  /// @return The archive bytes the scan has taken from storage.
  uint64_t getCompletedBytes() override;

  uint64_t getCompletedRows() override {
    return completedRows_;
  }

  /// @return The scan's timers and counters, as running totals over its
  /// splits. Velox replaces the values it has with these on each call. The
  /// metrics are described in docs/runtime-metrics.md.
  std::unordered_map<std::string, RuntimeMetric> getRuntimeStats() override;

 private:
  /// Recursively adds fields from the column type to the list of fields to be
  /// retrieved from the data source.
  ///
  /// @param columnType
  /// @param parentName The name of the parent field (used for nested fields).
  void addFieldsRecursively(
      const TypePtr& columnType,
      const std::string& parentName);

  /// Destroys the split's cursor, which closes its archive, and finishes the
  /// split's metrics. Does nothing without a cursor.
  void closeCursor();

  ClpConfig::StorageType storageType_;
  velox::memory::MemoryPool* pool_;
  RowTypePtr outputType_;
  std::set<std::string> columnUntypedNames_;
  uint64_t completedRows_{0};

  std::vector<search_lib::Field> fields_;

  std::unique_ptr<search_lib::BaseClpCursor> cursor_;
  std::shared_ptr<ClpS3AuthProviderBase> s3AuthProvider_;

  std::shared_ptr<search_lib::ClpScanStats> scanStats_;
  bool logSplitStats_{false};
  std::string scanLabel_;
  std::string splitPath_;
};

} // namespace facebook::velox::connector::clp
