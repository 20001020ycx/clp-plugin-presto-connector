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

#include <glog/logging.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "connector/ClpColumnHandle.h"
#include "connector/ClpConnectorSplit.h"
#include "connector/ClpDataSource.h"
#include "connector/ClpTableHandle.h"
#include "connector/search_lib/ClpS3AuthProviderBase.h"
#include "connector/search_lib/archive/ClpArchiveCursor.h"
#include "connector/search_lib/ir/ClpIrCursor.h"
#include "velox/vector/FlatVector.h"

namespace facebook::velox::connector::clp {

namespace {

using search_lib::ScanMetricUnit;
using search_lib::ScanPhase;
using search_lib::ScanPhaseTimer;

RuntimeCounter::Unit toVeloxUnit(ScanMetricUnit unit) {
  switch (unit) {
    case ScanMetricUnit::kNanos:
      return RuntimeCounter::Unit::kNanos;
    case ScanMetricUnit::kBytes:
      return RuntimeCounter::Unit::kBytes;
    case ScanMetricUnit::kNone:
    default:
      return RuntimeCounter::Unit::kNone;
  }
}

/// @return `value` as the contents of a JSON string.
std::string escapeJson(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char const c : value) {
    if ('"' == c || '\\' == c) {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  return escaped;
}

} // namespace

ClpDataSource::ClpDataSource(
    const RowTypePtr& outputType,
    const ConnectorTableHandlePtr& tableHandle,
    const connector::ColumnHandleMap& columnHandles,
    velox::memory::MemoryPool* pool,
    std::shared_ptr<const ClpConfig>& clpConfig,
    std::string scanLabel)
    : pool_(pool),
      outputType_(outputType),
      scanStats_(std::make_shared<search_lib::ClpScanStats>()),
      logSplitStats_(clpConfig->logSplitStats()),
      scanLabel_(std::move(scanLabel)) {
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
  // A split that the scan abandoned, for example once the query had enough
  // rows. The TableScan took its last snapshot of the metrics before this, so
  // this close reaches only the per-split log.
  closeCursor();
}

uint64_t ClpDataSource::getCompletedBytes() {
  return scanStats_->bytesConsumed();
}

std::unordered_map<std::string, RuntimeMetric>
ClpDataSource::getRuntimeStats() {
  std::unordered_map<std::string, RuntimeMetric> result;
  for (auto const& [name, metric] : scanStats_->metrics()) {
    RuntimeMetric runtimeMetric(toVeloxUnit(metric.unit));
    runtimeMetric.sum = metric.sum;
    runtimeMetric.count = metric.count;
    runtimeMetric.min = metric.min;
    runtimeMetric.max = metric.max;
    result.emplace(name, runtimeMetric);
  }
  return result;
}

void ClpDataSource::closeCursor() {
  if (nullptr == cursor_) {
    return;
  }
  {
    ScanPhaseTimer timer(scanStats_.get(), ScanPhase::kCloseArchive);
    cursor_.reset();
  }
  if (logSplitStats_) {
    LOG(INFO) << "CLP split stats {\"scan\":\"" << escapeJson(scanLabel_)
              << "\",\"path\":\"" << escapeJson(splitPath_)
              << "\",\"metrics\":" << scanStats_->describeSplit() << "}";
  }
  scanStats_->finishSplit();
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
  // The TableScan adds a split only after `next` has finished the previous one
  // and closed its cursor; this closes one that was left open regardless.
  closeCursor();

  auto clpSplit = std::dynamic_pointer_cast<ClpConnectorSplit>(split);
  scanStats_->beginSplit();
  // The split's path as the coordinator sent it, not the signed URL.
  splitPath_ = clpSplit->path_;
  ScanPhaseTimer timer(scanStats_.get(), ScanPhase::kAddSplit);

  std::string splitPath = clpSplit->path_;
  clp_s::InputSource inputSource;
  clp_s::NetworkAuthOption networkAuthOption{.method = clp_s::AuthMethod::None};
  if (ClpConfig::StorageType::kFs == storageType_) {
    inputSource = clp_s::InputSource::Filesystem;
  } else if (ClpConfig::StorageType::kS3 == storageType_) {
    inputSource = clp_s::InputSource::Network;
    splitPath = s3AuthProvider_->constructS3Url(clpSplit->path_);
    networkAuthOption = s3AuthProvider_->networkAuthOption();
  } else {
    VELOX_UNREACHABLE();
  }

  if (ClpConnectorSplit::SplitType::kArchive == clpSplit->type_) {
    cursor_ = std::make_unique<search_lib::ClpArchiveCursor>(
        inputSource, splitPath, std::move(networkAuthOption), scanStats_);
  } else if (ClpConnectorSplit::SplitType::kIr == clpSplit->type_) {
    cursor_ = std::make_unique<search_lib::ClpIrCursor>(
        inputSource, splitPath, std::move(networkAuthOption), true);
  } else {
    VELOX_UNSUPPORTED(
        "Unsupported split type: {}", static_cast<int>(clpSplit->type_));
  }

  auto pushDownQuery = clpSplit->kqlQuery_;
  if (pushDownQuery && !pushDownQuery->empty()) {
    cursor_->executeQuery(*pushDownQuery, fields_);
  } else {
    cursor_->executeQuery("*", fields_);
  }
}

std::optional<RowVectorPtr> ClpDataSource::next(
    uint64_t size,
    ContinueFuture& future) {
  RowVectorPtr result;
  {
    ScanPhaseTimer timer(scanStats_.get(), ScanPhase::kNext);
    auto rowsScanned = cursor_->fetchNext(size);
    auto rowsFiltered = cursor_->getNumFilteredRows();
    if (rowsFiltered != 0) {
      completedRows_ += rowsScanned;
      result = std::dynamic_pointer_cast<RowVector>(
          cursor_->createVector(pool_, outputType_, rowsFiltered));
    }
  }
  if (nullptr == result) {
    // The split is finished. Closing its archive now, rather than when the
    // next split arrives, lets the TableScan's end-of-split snapshot of
    // `getRuntimeStats` include the close, the last split's too.
    closeCursor();
  }
  return result;
}

} // namespace facebook::velox::connector::clp
