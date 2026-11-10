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

#include <optional>

#include "connector/ClpColumnHandle.h"
#include "connector/ClpConnectorSplit.h"
#include "connector/ClpDataSource.h"
#include "connector/ClpTableHandle.h"
#include "connector/search_lib/ClpS3AuthProviderBase.h"
#include "connector/search_lib/ClpScanStats.h"
#include "connector/search_lib/archive/ClpArchiveCursor.h"
#include "connector/search_lib/ir/ClpIrCursor.h"
#include "velox/vector/FlatVector.h"

namespace facebook::velox::connector::clp {

namespace {
using search_lib::ClpScanStats;
using search_lib::ScanPhase;

// thesis-metrics: adds the time from construction to destruction to one of
// the data source's outer phases.
class OuterScope {
 public:
  OuterScope(ClpScanStats& stats, ScanPhase phase)
      : stats_(stats), phase_(phase), begin_(ClpScanStats::mark()) {}

  ~OuterScope() {
    stats_.addOuter(phase_, begin_, ClpScanStats::mark());
  }

 private:
  ClpScanStats& stats_;
  const ScanPhase phase_;
  const ClpScanStats::Mark begin_;
};
} // namespace

ClpDataSource::ClpDataSource(
    const RowTypePtr& outputType,
    const ConnectorTableHandlePtr& tableHandle,
    const connector::ColumnHandleMap& columnHandles,
    velox::memory::MemoryPool* pool,
    std::shared_ptr<const ClpConfig>& clpConfig,
    const std::string& scanLabel)
    : pool_(pool),
      outputType_(outputType),
      scanStats_(std::make_shared<ClpScanStats>(scanLabel)) {
  auto clpTableHandle =
      std::dynamic_pointer_cast<const ClpTableHandle>(tableHandle);
  storageType_ = clpConfig->storageType();
  s3AuthProvider_ = clpConfig->s3AuthProvider();

  for (const auto& outputName : outputType->names()) {
    auto columnHandle = columnHandles.find(outputName);
    VELOX_CHECK(
        columnHandle != columnHandles.end(),
        "ColumnHandle not found for output name: {}",
        outputName);
    auto clpColumnHandle =
        std::dynamic_pointer_cast<const ClpColumnHandle>(columnHandle->second);
    VELOX_CHECK_NOT_NULL(
        clpColumnHandle,
        "ColumnHandle must be an instance of ClpColumnHandle for output name: {}",
        outputName);
    auto columnName = clpColumnHandle->originalColumnName();
    auto columnType = clpColumnHandle->columnType();
    addFieldsRecursively(columnType, columnName);
  }
}

ClpDataSource::~ClpDataSource() {
  auto const begin = ClpScanStats::mark();
  cursor_.reset();
  if (scanStats_->splitOpen()) {
    scanStats_->addOuter(ScanPhase::kCloseArchive, begin, ClpScanStats::mark());
    scanStats_->finishSplit();
  }
}

uint64_t ClpDataSource::getCompletedBytes() {
  return scanStats_->bytesConsumedTotal();
}

std::unordered_map<std::string, RuntimeMetric>
ClpDataSource::getRuntimeStats() {
  return scanStats_->runtimeStats();
}

void ClpDataSource::addFieldsRecursively(
    const TypePtr& columnType,
    const std::string& parentName) {
  if (columnType->kind() == TypeKind::ROW) {
    const auto& rowType = columnType->asRow();
    for (uint32_t i = 0; i < rowType.size(); ++i) {
      const auto& childType = rowType.childAt(i);
      const auto childName = parentName + "." + rowType.nameOf(i);
      addFieldsRecursively(childType, childName);
    }
  } else {
    search_lib::ColumnType clpColumnType = search_lib::ColumnType::Unknown;
    switch (columnType->kind()) {
      case TypeKind::BOOLEAN:
        clpColumnType = search_lib::ColumnType::Boolean;
        break;
      case TypeKind::INTEGER:
      case TypeKind::BIGINT:
      case TypeKind::SMALLINT:
      case TypeKind::TINYINT:
        clpColumnType = search_lib::ColumnType::Integer;
        break;
      case TypeKind::DOUBLE:
      case TypeKind::REAL:
        clpColumnType = search_lib::ColumnType::Float;
        break;
      case TypeKind::VARCHAR:
        clpColumnType = search_lib::ColumnType::String;
        break;
      case TypeKind::ARRAY:
        clpColumnType = search_lib::ColumnType::Array;
        break;
      case TypeKind::TIMESTAMP:
        clpColumnType = search_lib::ColumnType::Timestamp;
        break;
      default:
        VELOX_USER_FAIL("Type not supported: {}", columnType->name());
    }
    fields_.emplace_back(search_lib::Field{clpColumnType, parentName});
  }
}

void ClpDataSource::addSplit(std::shared_ptr<ConnectorSplit> split) {
  auto const addSplitBegin = ClpScanStats::mark();
  auto clpSplit = std::dynamic_pointer_cast<ClpConnectorSplit>(split);

  std::string splitPath = clpSplit->path_;
  clp_s::InputSource inputSource;
  if (ClpConfig::StorageType::kFs == storageType_) {
    inputSource = clp_s::InputSource::Filesystem;
  } else if (ClpConfig::StorageType::kS3 == storageType_) {
    inputSource = clp_s::InputSource::Network;
    splitPath = s3AuthProvider_->constructS3Url(clpSplit->path_);
  } else {
    VELOX_UNREACHABLE();
  }

  std::unique_ptr<search_lib::BaseClpCursor> newCursor;
  if (ClpConnectorSplit::SplitType::kArchive == clpSplit->type_) {
    newCursor = std::make_unique<search_lib::ClpArchiveCursor>(
        inputSource, splitPath, scanStats_);
  } else if (ClpConnectorSplit::SplitType::kIr == clpSplit->type_) {
    newCursor =
        std::make_unique<search_lib::ClpIrCursor>(inputSource, splitPath, true);
  } else {
    VELOX_UNSUPPORTED(
        "Unsupported split type: {}", static_cast<int>(clpSplit->type_));
  }

  // thesis-metrics: as before, the new cursor exists before the assignment
  // destroys the previous split's cursor, which closes that split's archive.
  // The time of that is the previous split's; the rest of this call is the new
  // split's.
  auto const closeBegin = ClpScanStats::mark();
  cursor_ = std::move(newCursor);
  auto const closeEnd = ClpScanStats::mark();
  if (scanStats_->splitOpen()) {
    scanStats_->addOuter(ScanPhase::kCloseArchive, closeBegin, closeEnd);
    scanStats_->finishSplit();
  }
  // The split's path as the coordinator sent it, not the signed URL.
  scanStats_->beginSplit(clpSplit->path_);
  scanStats_->addOuter(ScanPhase::kAddSplit, addSplitBegin, closeBegin);

  auto pushDownQuery = clpSplit->kqlQuery_;
  if (pushDownQuery && !pushDownQuery->empty()) {
    cursor_->executeQuery(*pushDownQuery, fields_);
  } else {
    cursor_->executeQuery("*", fields_);
  }
  scanStats_->addOuter(
      ScanPhase::kAddSplit, closeEnd, ClpScanStats::mark(), false);
}

std::optional<RowVectorPtr> ClpDataSource::next(
    uint64_t size,
    ContinueFuture& future) {
  OuterScope scope(*scanStats_, ScanPhase::kNext);
  auto rowsScanned = cursor_->fetchNext(size);
  auto rowsFiltered = cursor_->getNumFilteredRows();
  if (rowsFiltered == 0) {
    return nullptr;
  }
  completedRows_ += rowsScanned;
  return std::dynamic_pointer_cast<RowVector>(
      cursor_->createVector(pool_, outputType_, rowsFiltered));
}

} // namespace facebook::velox::connector::clp
