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

#include <cstdint>

#include "clp_s/ColumnReader.hpp"
#include "connector/search_lib/archive/ClpArchiveJsonStringVectorLoader.h"
#include "velox/vector/ComplexVector.h"

namespace facebook::velox::connector::clp::search_lib {

ClpArchiveJsonStringVectorLoader::ClpArchiveJsonStringVectorLoader(
    clp_s::SchemaReader* schemaReader,
    const std::shared_ptr<std::vector<uint64_t>>& filteredRowIndices)
    : schemaReader_(schemaReader), filteredRowIndices_(filteredRowIndices) {
  schemaReader_->initialize_serializer();
}

void ClpArchiveJsonStringVectorLoader::loadInternal(
    RowSet rows,
    ValueHook* hook,
    vector_size_t resultSize,
    VectorPtr* result) {
  VELOX_CHECK_NOT_NULL(result, "result vector must not be null");
  VELOX_CHECK_NULL(
      hook, "ClpArchiveJsonStringVectorLoader doesn't support ValueHook");

  auto vector = *result;
  auto* stringVector = vector->asFlatVector<StringView>();
  for (vector_size_t const vectorIndex : rows) {
    auto messageIndex = filteredRowIndices_->at(vectorIndex);
    auto jsonString = schemaReader_->generate_json_string(messageIndex);
    stringVector->set(vectorIndex, StringView(jsonString));
    stringVector->setNull(vectorIndex, false);
  }
}

} // namespace facebook::velox::connector::clp::search_lib
