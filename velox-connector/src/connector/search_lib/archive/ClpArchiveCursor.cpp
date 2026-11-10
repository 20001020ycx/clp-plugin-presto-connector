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

#include "clp_s/ArchiveReader.hpp"
#include "clp_s/SingleFileArchiveDefs.hpp"
#include "clp_s/search/EvaluateTimestampIndex.hpp"
#include "clp_s/search/ast/EmptyExpr.hpp"
#include "clp_s/search/ast/SearchUtils.hpp"
#include "clp_s/search/ast/SetTimestampLiteralPrecision.hpp"
#include "clp_s/search/ast/TimestampLiteral.hpp"
#include "connector/ClpColumnHandle.h"
#include "connector/search_lib/ClpScanStats.h"
#include "connector/search_lib/archive/ClpArchiveCursor.h"
#include "connector/search_lib/archive/ClpArchiveJsonStringVectorLoader.h"
#include "connector/search_lib/archive/ClpArchiveVectorLoader.h"
#include "connector/search_lib/archive/ClpQueryRunner.h"

using namespace clp_s;
using namespace clp_s::search;
using namespace clp_s::search::ast;

namespace facebook::velox::connector::clp::search_lib {

ClpArchiveCursor::ClpArchiveCursor(
    clp_s::InputSource inputSource,
    std::string_view splitPath,
    std::shared_ptr<ClpScanStats> scanStats)
    : BaseClpCursor(inputSource, splitPath, std::move(scanStats)),
      archiveReader_(std::make_shared<ArchiveReader>()),
      filteredRowIndices_(std::make_shared<std::vector<uint64_t>>()) {}

ClpArchiveCursor::~ClpArchiveCursor() {
  if (currentSplitLoaded_) {
    archiveReader_->close();
  }
}

uint64_t ClpArchiveCursor::fetchNext(uint64_t numRows) {
  filteredRowIndices_->clear();
  projectedColumnIndex_ = 0;
  columnIndex_ = 0;

  if (ErrorCode::Success != errorCode_) {
    return 0;
  }

  if (false == currentSplitLoaded_) {
    PhaseLap loadLap(scanStats_.get());
    errorCode_ = loadSplit();
    loadLap.mark(ScanPhase::kLoadSplit);
    if (ErrorCode::Success != errorCode_) {
      return 0;
    }
  }

  PhaseLap lap(scanStats_.get());
  while (currentSchemaIndex_ < matchedSchemas_.size()) {
    if (false == currentSchemaTableLoaded_) {
      currentSchemaId_ = matchedSchemas_[currentSchemaIndex_];
      if (EvaluatedValue::False ==
          queryRunner_->schema_init(currentSchemaId_)) {
        lap.mark(ScanPhase::kSchemaInit);
        currentSchemaIndex_ += 1;
        currentSchemaTableLoaded_ = false;
        errorCode_ = ErrorCode::DictionaryNotFound;
        continue;
      }

      lap.mark(ScanPhase::kSchemaInit);
      schemaReader_ =
          &archiveReader_->read_schema_table(currentSchemaId_, false, false);
      lap.mark(ScanPhase::kReadTable);
      schemaReader_->initialize_filter_with_column_map(queryRunner_.get());
      lap.mark(ScanPhase::kFilterInit);
      if (scanStats_) {
        scanStats_->addCounter(ScanCounter::kTablesRead, 1);
      }

      errorCode_ = ErrorCode::Success;
      currentSchemaTableLoaded_ = true;
    }

    auto rowsScanned = queryRunner_->fetchNext(numRows, filteredRowIndices_);
    lap.mark(ScanPhase::kFilter);
    if (scanStats_) {
      scanStats_->addCounter(ScanCounter::kRowsScanned, rowsScanned);
      scanStats_->addCounter(
          ScanCounter::kRowsMatched, filteredRowIndices_->size());
    }
    if (false == filteredRowIndices_->empty()) {
      return rowsScanned;
    }

    currentSchemaIndex_ += 1;
    currentSchemaTableLoaded_ = false;
  }

  return 0;
}

size_t ClpArchiveCursor::getNumFilteredRows() const {
  return filteredRowIndices_->size();
}

VectorPtr ClpArchiveCursor::createVector(
    memory::MemoryPool* pool,
    const TypePtr& vectorType,
    size_t vectorSize) {
  auto projectedColumns = getProjectedColumns();
  VELOX_CHECK_EQ(
      projectedColumns.size() + jsonStringColumnIndices_.size(),
      outputColumns_.size(),
      "Projected columns size {} does not match fields size {}",
      projectedColumns.size(),
      outputColumns_.size());
  PhaseLap lap(scanStats_.get());
  auto vector =
      createVectorHelper(pool, vectorType, vectorSize, projectedColumns);
  lap.mark(ScanPhase::kCreateVector);
  return vector;
}

const std::vector<clp_s::BaseColumnReader*>&
ClpArchiveCursor::getProjectedColumns() const {
  if (queryRunner_) {
    return queryRunner_->getProjectedColumns();
  }
  static std::vector<clp_s::BaseColumnReader*> const kEmpty;
  return kEmpty;
}

ErrorCode ClpArchiveCursor::loadSplit() {
  auto networkAuthOption = inputSource_ == InputSource::Filesystem
      ? NetworkAuthOption{.method = AuthMethod::None}
      : NetworkAuthOption{.method = AuthMethod::S3PresignedUrlV4};

  PhaseLap lap(scanStats_.get());
  try {
    archiveReader_->open(
        get_path_object_for_raw_path(splitPath_), networkAuthOption);
  } catch (std::exception& e) {
    VLOG(2) << "Failed to open archive file: " << e.what();
    return ErrorCode::InternalError;
  }
  lap.mark(ScanPhase::kOpenArchive);

  auto timestampDict = archiveReader_->get_timestamp_dictionary();
  auto schemaTree = archiveReader_->get_schema_tree();
  auto schemaMap = archiveReader_->get_schema_map();

  auto const defaultTimestampPrecision{
      archiveReader_->has_deprecated_timestamp_format()
          ? TimestampLiteral::Precision::Milliseconds
          : TimestampLiteral::Precision::Nanoseconds};
  SetTimestampLiteralPrecision timestampPrecisionPass{
      defaultTimestampPrecision};
  expr_ = timestampPrecisionPass.run(expr_);

  EvaluateTimestampIndex timestampIndex(timestampDict);
  if (clp_s::EvaluatedValue::False == timestampIndex.run(expr_)) {
    VLOG(2) << "No matching timestamp ranges for query '" << query_ << "'";
    return ErrorCode::InvalidTimestampRange;
  }

  schemaMatch_ = std::make_shared<SchemaMatch>(schemaTree, schemaMap);
  if (expr_ = schemaMatch_->run(expr_);
      std::dynamic_pointer_cast<EmptyExpr>(expr_)) {
    VLOG(2) << "No matching schemas for query '" << query_ << "'";
    return ErrorCode::SchemaNotFound;
  }
  lap.mark(ScanPhase::kTimestampIndexAndSchemaMatch);

  projection_ = std::make_shared<Projection>(
      outputColumns_.empty() ? ReturnAllColumns : ReturnSelectedColumns, true);
  try {
    for (size_t i = 0; i < outputColumns_.size(); i++) {
      auto const& column = outputColumns_[i];
      if (ClpColumnHandle::jsonStringColumnName_ == column.name) {
        jsonStringColumnIndices_.insert(i);
        continue;
      }
      std::vector<std::string> descriptorTokens;
      std::string descriptorNamespace;
      if (false ==
          tokenize_column_descriptor(
              column.name, descriptorTokens, descriptorNamespace)) {
        VLOG(2) << "Can not tokenize invalid column: '" << column.name << "'";
        return ErrorCode::InternalError;
      }

      auto columnDescriptor = ColumnDescriptor::create_from_escaped_tokens(
          descriptorTokens, descriptorNamespace);
      switch (column.type) {
        case ColumnType::String:
          columnDescriptor->set_matching_types(
              LiteralType::ClpStringT | LiteralType::VarStringT);
          break;
        case ColumnType::Integer:
          columnDescriptor->set_matching_types(LiteralType::IntegerT);
          break;
        case ColumnType::Float:
          columnDescriptor->set_matching_types(LiteralType::FloatT);
          break;
        case ColumnType::Boolean:
          columnDescriptor->set_matching_types(LiteralType::BooleanT);
          break;
        case ColumnType::Array:
          columnDescriptor->set_matching_types(LiteralType::ArrayT);
          break;
        case ColumnType::Timestamp:
          columnDescriptor->set_matching_types(
              LiteralType::TimestampT | LiteralType::IntegerT |
              LiteralType::FloatT);
          break;
        default:
          break;
      }

      projection_->add_column(columnDescriptor);
    }
  } catch (TraceableException& e) {
    VLOG(2) << e.what();
    return ErrorCode::InternalError;
  }
  projection_->resolve_columns(schemaTree);
  lap.mark(ScanPhase::kProjection);

  archiveReader_->read_metadata();
  lap.mark(ScanPhase::kReadTableMetadata);

  matchedSchemas_.clear();
  for (auto schemaId : archiveReader_->get_schema_ids()) {
    if (schemaMatch_->schema_matched(schemaId)) {
      matchedSchemas_.push_back(schemaId);
    }
  }
  if (scanStats_) {
    scanStats_->addCounter(
        ScanCounter::kSchemasInArchive, archiveReader_->get_schema_ids().size());
    scanStats_->addCounter(
        ScanCounter::kSchemasMatched, matchedSchemas_.size());
  }

  if (matchedSchemas_.empty()) {
    return ErrorCode::SchemaNotFound;
  }

  EvaluateTimestampIndex timestamp_index(timestampDict);
  if (EvaluatedValue::False == timestamp_index.run(expr_)) {
    VLOG(2) << "No matching timestamp ranges for query '" << query_ << "'";
    return ErrorCode::InvalidTimestampRange;
  }
  lap.mark(ScanPhase::kTimestampIndexAndSchemaMatch);

  archiveReader_->read_variable_dictionary();
  archiveReader_->read_log_type_dictionary();
  archiveReader_->read_array_dictionary();
  lap.mark(ScanPhase::kLoadDictionaries);

  currentSchemaIndex_ = 0;
  currentSchemaTableLoaded_ = false;

  archiveReader_->open_packed_streams();
  lap.mark(ScanPhase::kOpenPackedStreams);
  currentSplitLoaded_ = true;
  queryRunner_ = std::make_shared<ClpQueryRunner>(
      schemaMatch_, expr_, archiveReader_, false, projection_);
  queryRunner_->global_init();
  lap.mark(ScanPhase::kQueryRunnerInit);
  return ErrorCode::Success;
}

VectorPtr ClpArchiveCursor::createVectorHelper(
    memory::MemoryPool* pool,
    const TypePtr& vectorType,
    size_t vectorSize,
    const std::vector<clp_s::BaseColumnReader*>& projectedColumns) {
  if (vectorType->kind() == TypeKind::ROW) {
    auto& rowType = vectorType->as<TypeKind::ROW>();
    const uint32_t numChildren = rowType.size();
    std::vector<VectorPtr> children;
    children.reserve(numChildren);

    for (uint32_t i = 0; i < numChildren; ++i) {
      children.push_back(createVectorHelper(
          pool, rowType.childAt(i), vectorSize, projectedColumns));
    }
    return std::make_shared<RowVector>(
        pool, vectorType, nullptr, vectorSize, std::move(children));
  }
  auto vector = BaseVector::create(vectorType, vectorSize, pool);
  vector->setNulls(allocateNulls(vectorSize, pool, bits::kNull));

  if (jsonStringColumnIndices_.contains(columnIndex_)) {
    ++columnIndex_;
    return std::make_shared<LazyVector>(
        pool,
        vectorType,
        vectorSize,
        std::make_unique<ClpArchiveJsonStringVectorLoader>(
            schemaReader_, filteredRowIndices_, scanStats_),
        std::move(vector));
  }

  VELOX_CHECK_LT(
      projectedColumnIndex_,
      projectedColumns.size(),
      "Projected column index out of bounds");
  auto* projectedColumn = projectedColumns[projectedColumnIndex_];
  auto projectedType = outputColumns_[columnIndex_].type;
  projectedColumnIndex_++;
  columnIndex_++;
  return std::make_shared<LazyVector>(
      pool,
      vectorType,
      vectorSize,
      std::make_unique<ClpArchiveVectorLoader>(
          projectedColumn, projectedType, filteredRowIndices_, scanStats_),
      std::move(vector));
}

} // namespace facebook::velox::connector::clp::search_lib
