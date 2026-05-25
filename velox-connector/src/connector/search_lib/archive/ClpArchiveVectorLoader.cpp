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

#include <cmath>
#include <cstdint>
#include <utility>

#include "clp_s/ColumnReader.hpp"
#include "clp_s/SchemaTree.hpp"
#include "connector/search_lib/BaseClpCursor.h"
#include "connector/search_lib/ClpTimestampsUtils.h"
#include "connector/search_lib/archive/ClpArchiveVectorLoader.h"
#include "velox/type/Timestamp.h"
#include "velox/vector/ComplexVector.h"
#include "velox/vector/FlatVector.h"

namespace facebook::velox::connector::clp::search_lib {

ClpArchiveVectorLoader::ClpArchiveVectorLoader(
    clp_s::BaseColumnReader* columnReader,
    ColumnType nodeType,
    const std::shared_ptr<std::vector<uint64_t>> filteredRowIndices)
    : columnReader_(columnReader),
      nodeType_(nodeType),
      filteredRowIndices_(std::move(filteredRowIndices)) {}

template <typename T, typename VectorPtr>
void ClpArchiveVectorLoader::populateData(RowSet rows, VectorPtr vector) {
  if (columnReader_ == nullptr) {
    for (int vectorIndex : rows) {
      vector->setNull(vectorIndex, true);
    }
    return;
  }

  for (int vectorIndex : rows) {
    auto messageIndex = filteredRowIndices_->at(vectorIndex);

    if constexpr (std::is_same_v<T, std::string>) {
      auto string_value =
          std::get<std::string>(columnReader_->extract_value(messageIndex));
      vector->set(vectorIndex, StringView(string_value));
    } else {
      vector->set(
          vectorIndex, std::get<T>(columnReader_->extract_value(messageIndex)));
    }

    vector->setNull(vectorIndex, false);
  }
}

template <clp_s::NodeType Type>
void ClpArchiveVectorLoader::populateTimestampData(
    RowSet rows,
    FlatVector<facebook::velox::Timestamp>* vector) {
  bool supportedNodeType{false};
  switch (Type) {
    case clp_s::NodeType::Float:
    case clp_s::NodeType::FormattedFloat:
    case clp_s::NodeType::DictionaryFloat:
    case clp_s::NodeType::Integer:
    case clp_s::NodeType::DeprecatedDateString:
    case clp_s::NodeType::Timestamp:
      supportedNodeType = true;
      break;
    default:
      break;
  }
  if (columnReader_ == nullptr || false == supportedNodeType) {
    for (int vectorIndex : rows) {
      vector->setNull(vectorIndex, true);
    }
    return;
  }

  for (int vectorIndex : rows) {
    auto messageIndex = filteredRowIndices_->at(vectorIndex);

    if (clp_s::NodeType::Timestamp == Type) {
      auto reader{static_cast<clp_s::TimestampColumnReader*>(columnReader_)};
      vector->set(
          vectorIndex,
          convertNanosecondEpochToVeloxTimestamp(
              reader->get_encoded_time(messageIndex)));
    } else if (clp_s::NodeType::Float == Type) {
      auto reader = static_cast<clp_s::FloatColumnReader*>(columnReader_);
      vector->set(
          vectorIndex,
          convertToVeloxTimestamp(
              std::get<double>(reader->extract_value(messageIndex))));
    } else if (clp_s::NodeType::FormattedFloat == Type) {
      auto reader =
          static_cast<clp_s::FormattedFloatColumnReader*>(columnReader_);
      vector->set(
          vectorIndex,
          convertToVeloxTimestamp(
              std::get<double>(reader->extract_value(messageIndex))));
    } else if (clp_s::NodeType::DictionaryFloat == Type) {
      auto reader =
          static_cast<clp_s::DictionaryFloatColumnReader*>(columnReader_);
      vector->set(
          vectorIndex,
          convertToVeloxTimestamp(
              std::get<double>(reader->extract_value(messageIndex))));
    } else if (clp_s::NodeType::Integer == Type) {
      auto reader = static_cast<clp_s::Int64ColumnReader*>(columnReader_);
      vector->set(
          vectorIndex,
          convertToVeloxTimestamp(
              std::get<int64_t>(reader->extract_value(messageIndex))));
    } else {
      auto reader =
          static_cast<clp_s::DeprecatedDateStringColumnReader*>(columnReader_);
      vector->set(
          vectorIndex,
          convertToVeloxTimestamp(reader->get_encoded_time(messageIndex)));
    }
  }
}

void ClpArchiveVectorLoader::loadInternal(
    RowSet rows,
    ValueHook* hook,
    vector_size_t resultSize,
    VectorPtr* result) {
  VELOX_CHECK_NOT_NULL(result, "result vector must not be null");

  auto vector = *result;
  switch (nodeType_) {
    case ColumnType::Integer: {
      auto intVector = vector->asFlatVector<int64_t>();
      populateData<int64_t>(rows, intVector);
      break;
    }
    case ColumnType::Float: {
      auto floatVector = vector->asFlatVector<double>();
      populateData<double>(rows, floatVector);
      break;
    }
    case ColumnType::Boolean: {
      auto boolVector = vector->asFlatVector<bool>();
      populateData<uint8_t>(rows, boolVector);
      break;
    }
    case ColumnType::String: {
      auto stringVector = vector->asFlatVector<StringView>();
      populateData<std::string>(rows, stringVector);
      break;
    }
    case ColumnType::Array: {
      if (columnReader_ == nullptr) {
        for (int vectorIndex : rows) {
          vector->setNull(vectorIndex, true);
        }
        return;
      }
      auto arrayVector = std::dynamic_pointer_cast<ArrayVector>(vector);
      auto elements = arrayVector->elements()->asFlatVector<StringView>();
      vector_size_t elementIndex = 0;

      for (int vectorIndex : rows) {
        auto messageIndex = filteredRowIndices_->at(vectorIndex);

        auto jsonString =
            std::get<std::string>(columnReader_->extract_value(messageIndex));

        simdjson::padded_string padded(jsonString);
        simdjson::ondemand::document doc;
        try {
          doc = arrayParser_->iterate(padded);
        } catch (const simdjson::simdjson_error& e) {
          VELOX_FAIL("JSON parse error at row {}: {}", vectorIndex, e.what());
        }

        simdjson::ondemand::array array;
        try {
          array = doc.get_array();
        } catch (const simdjson::simdjson_error& e) {
          VELOX_FAIL(
              "Expected JSON array at row {}: {}", vectorIndex, e.what());
        }

        std::vector<std::string_view> arrayElements;
        for (auto arrayElement : array) {
          arrayElements.emplace_back(
              simdjson::to_json_string(arrayElement).value());
        }

        if (elementIndex + arrayElements.size() > elements->size()) {
          size_t newSize = std::max<size_t>(
              elementIndex + arrayElements.size(),
              static_cast<size_t>(elements->size()) * 2);
          elements->resize(newSize);
        }

        arrayVector->setOffsetAndSize(
            vectorIndex, elementIndex, arrayElements.size());
        for (auto& arrayElement : arrayElements) {
          elements->set(elementIndex++, StringView(arrayElement));
        }

        arrayVector->setNull(vectorIndex, false);
      }
      break;
    }
    case ColumnType::Timestamp: {
      auto timestampVector = vector->asFlatVector<Timestamp>();
      if (nullptr !=
          dynamic_cast<clp_s::TimestampColumnReader*>(columnReader_)) {
        populateTimestampData<clp_s::NodeType::Timestamp>(
            rows, timestampVector);
      } else if (
          nullptr != dynamic_cast<clp_s::Int64ColumnReader*>(columnReader_)) {
        populateTimestampData<clp_s::NodeType::Integer>(rows, timestampVector);
      } else if (
          nullptr !=
          dynamic_cast<clp_s::DeprecatedDateStringColumnReader*>(
              columnReader_)) {
        populateTimestampData<clp_s::NodeType::DeprecatedDateString>(
            rows, timestampVector);
      } else if (
          nullptr != dynamic_cast<clp_s::FloatColumnReader*>(columnReader_)) {
        populateTimestampData<clp_s::NodeType::Float>(rows, timestampVector);
      } else if (
          nullptr !=
          dynamic_cast<clp_s::FormattedFloatColumnReader*>(columnReader_)) {
        populateTimestampData<clp_s::NodeType::FormattedFloat>(
            rows, timestampVector);
      } else if (
          nullptr !=
          dynamic_cast<clp_s::DictionaryFloatColumnReader*>(columnReader_)) {
        populateTimestampData<clp_s::NodeType::DictionaryFloat>(
            rows, timestampVector);
      } else {
        populateTimestampData<clp_s::NodeType::Unknown>(rows, timestampVector);
      }
      break;
    }
    default:
      VELOX_FAIL("Unsupported column type");
  }
}

// Explicit template instantiations for linker
template void ClpArchiveVectorLoader::populateData<int64_t>(
    RowSet rows,
    FlatVector<int64_t>* vector);
template void ClpArchiveVectorLoader::populateData<double>(
    RowSet rows,
    FlatVector<double>* vector);
template void ClpArchiveVectorLoader::populateData<uint8_t>(
    RowSet rows,
    FlatVector<bool>* vector);
template void ClpArchiveVectorLoader::populateData<std::string>(
    RowSet rows,
    FlatVector<StringView>* vector);
template void
ClpArchiveVectorLoader::populateTimestampData<clp_s::NodeType::Timestamp>(
    RowSet rows,
    FlatVector<facebook::velox::Timestamp>* vector);
template void
ClpArchiveVectorLoader::populateTimestampData<clp_s::NodeType::Float>(
    RowSet rows,
    FlatVector<facebook::velox::Timestamp>* vector);
template void
ClpArchiveVectorLoader::populateTimestampData<clp_s::NodeType::FormattedFloat>(
    RowSet rows,
    FlatVector<facebook::velox::Timestamp>* vector);
template void
ClpArchiveVectorLoader::populateTimestampData<clp_s::NodeType::DictionaryFloat>(
    RowSet rows,
    FlatVector<facebook::velox::Timestamp>* vector);
template void
ClpArchiveVectorLoader::populateTimestampData<clp_s::NodeType::Integer>(
    RowSet rows,
    FlatVector<facebook::velox::Timestamp>* vector);
template void ClpArchiveVectorLoader::populateTimestampData<
    clp_s::NodeType::DeprecatedDateString>(
    RowSet rows,
    FlatVector<facebook::velox::Timestamp>* vector);
template void
ClpArchiveVectorLoader::populateTimestampData<clp_s::NodeType::Unknown>(
    RowSet rows,
    FlatVector<facebook::velox::Timestamp>* vector);

} // namespace facebook::velox::connector::clp::search_lib
