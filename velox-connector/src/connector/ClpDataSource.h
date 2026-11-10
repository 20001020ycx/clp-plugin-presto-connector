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

#include "velox/connectors/Connector.h"
#include "connector/ClpConfig.h"
#include "connector/search_lib/BaseClpCursor.h"

namespace clp_s {
class BaseColumnReader;
} // namespace clp_s

namespace facebook::velox::connector::clp {

class ClpS3AuthProviderBase;

class ClpDataSource : public DataSource {
 public:
  ClpDataSource(
      const RowTypePtr& outputType,
      const ConnectorTableHandlePtr& tableHandle,
      const connector::ColumnHandleMap& columnHandles,
      velox::memory::MemoryPool* pool,
      std::shared_ptr<const ClpConfig>& clpConfig,
      const std::string& scanLabel = "");

  // thesis-metrics: times the closing of the last split's archive.
  ~ClpDataSource() override;

  void addSplit(std::shared_ptr<ConnectorSplit> split) override;

  std::optional<RowVectorPtr> next(uint64_t size, velox::ContinueFuture& future)
      override;

  void addDynamicFilter(
      column_index_t outputChannel,
      const std::shared_ptr<common::Filter>& filter) override {
    VELOX_NYI("Dynamic filters not supported by ClpConnector.");
  }

  // thesis-metrics: bytes the scan took from the network readers.
  uint64_t getCompletedBytes() override;

  uint64_t getCompletedRows() override {
    return completedRows_;
  }

  // thesis-metrics: the scan's timers and counters, see ClpScanStats.h.
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

  ClpConfig::StorageType storageType_;
  velox::memory::MemoryPool* pool_;
  RowTypePtr outputType_;
  std::set<std::string> columnUntypedNames_;
  uint64_t completedRows_{0};
  uint64_t completedBytes_{0};

  std::vector<search_lib::Field> fields_;

  std::unique_ptr<search_lib::BaseClpCursor> cursor_;
  std::shared_ptr<ClpS3AuthProviderBase> s3AuthProvider_;
  std::shared_ptr<search_lib::ClpScanStats> scanStats_;
};

} // namespace facebook::velox::connector::clp
