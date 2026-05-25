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

#include "connector/search_lib/ir/ClpIrVectorLoader.h"

#include "connector/search_lib/BaseClpCursor.h"
#include "connector/search_lib/ClpTimestampsUtils.h"

namespace facebook::velox::connector::clp::search_lib {

void ClpIrVectorLoader::loadInternal(
    RowSet rows,
    ValueHook* hook,
    vector_size_t resultSize,
    VectorPtr* result) {
  auto vector = *result;
  for (int vectorIndex : rows) {
    vector->setNull(vectorIndex, true);
    if (!isResolved_) {
      continue;
    }
    auto& logEvent = filteredLogEvents_->at(vectorIndex);
    // TODO: also need to support auto-generated keys
    auto userGenNodeIdValueMap = logEvent->get_user_gen_node_id_value_pairs();
    auto valueIt = userGenNodeIdValueMap.end();
    ::clp::ffi::SchemaTree::Node::id_t nodeId{};
    for (auto const candidateNodeId : nodeIds_) {
      valueIt = userGenNodeIdValueMap.find(candidateNodeId);
      if (valueIt != userGenNodeIdValueMap.end()) {
        nodeId = candidateNodeId;
        break;
      }
    }
    if (userGenNodeIdValueMap.end() == valueIt ||
        false == valueIt->second.has_value()) {
      continue;
    }
    auto const& value{valueIt->second};
    switch (nodeType_) {
      case ColumnType::String: {
        auto stringVector = vector->asFlatVector<StringView>();
        if (value->is<std::string>()) {
          auto stringValue = value->get_immutable_view<std::string>();
          stringVector->set(vectorIndex, StringView(stringValue));
        } else if (value->is<::clp::ffi::EightByteEncodedTextAst>()) {
          auto decodeResult =
              value->get_immutable_view<::clp::ffi::EightByteEncodedTextAst>()
                  .to_string();
          if (!decodeResult.has_value()) {
            continue;
          }
          stringVector->set(vectorIndex, StringView(decodeResult.value()));
        } else if (value->is<::clp::ffi::FourByteEncodedTextAst>()) {
          auto decodeResult =
              value->get_immutable_view<::clp::ffi::FourByteEncodedTextAst>()
                  .to_string();
          if (!decodeResult.has_value()) {
            continue;
          }
          stringVector->set(vectorIndex, StringView(decodeResult.value()));
        } else {
          continue;
        }
        vector->setNull(vectorIndex, false);
        break;
      }
      case ColumnType::Integer: {
        auto intVector = vector->asFlatVector<int64_t>();
        intVector->set(
            vectorIndex, value->get_immutable_view<::clp::ffi::value_int_t>());
        vector->setNull(vectorIndex, false);
        break;
      }
      case ColumnType::Float: {
        auto floatVector = vector->asFlatVector<double>();
        floatVector->set(
            vectorIndex,
            value->get_immutable_view<::clp::ffi::value_float_t>());
        vector->setNull(vectorIndex, false);
        break;
      }
      case ColumnType::Boolean: {
        auto boolVector = vector->asFlatVector<bool>();
        boolVector->set(
            vectorIndex, value->get_immutable_view<::clp::ffi::value_bool_t>());
        vector->setNull(vectorIndex, false);
        break;
      }
      case ColumnType::Timestamp: {
        auto timestampVector = vector->asFlatVector<Timestamp>();
        if (value->is<double>()) {
          timestampVector->set(
              vectorIndex,
              convertToVeloxTimestamp(value->get_immutable_view<double>()));
        } else if (value->is<int64_t>()) {
          timestampVector->set(
              vectorIndex,
              convertToVeloxTimestamp(value->get_immutable_view<int64_t>()));
        } else {
          VELOX_FAIL("Unsupported timestamp type");
        }
        break;
      }
      case ColumnType::Array: {
        auto arrayVector = std::dynamic_pointer_cast<ArrayVector>(vector);
        std::string jsonString;
        if (value->is<::clp::ffi::EightByteEncodedTextAst>()) {
          auto decodeResult =
              value->get_immutable_view<::clp::ffi::EightByteEncodedTextAst>()
                  .to_string();
          if (!decodeResult.has_value()) {
            continue;
          }
          jsonString = std::move(decodeResult.value());
        } else {
          auto decodeResult =
              value->get_immutable_view<::clp::ffi::FourByteEncodedTextAst>()
                  .to_string();
          if (!decodeResult.has_value()) {
            continue;
          }
          jsonString = std::move(decodeResult.value());
        }

        size_t numElements{0ULL};
        auto elements = arrayVector->elements()->asFlatVector<StringView>();
        auto obj = arrayParser_.iterate(jsonString);
        std::vector<std::string_view> rawElements;
        for (auto arrayElement : obj.get_array()) {
          auto raw_element = simdjson::to_json_string(arrayElement).value();
          rawElements.emplace_back(raw_element);
        }
        elements->resize(rawElements.size());
        for (auto& raw_element : rawElements) {
          elements->set(numElements++, StringView(raw_element));
        }
        arrayVector->setOffsetAndSize(vectorIndex, 0ULL, numElements);
        arrayVector->setNull(vectorIndex, false);
        break;
      }
      default:
        VELOX_FAIL("Unsupported column type");
    }
  }
}

} // namespace facebook::velox::connector::clp::search_lib
