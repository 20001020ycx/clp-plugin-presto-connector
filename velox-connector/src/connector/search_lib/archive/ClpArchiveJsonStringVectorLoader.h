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

#include "clp_s/SchemaReader.hpp"
#include "velox/vector/FlatVector.h"
#include "velox/vector/LazyVector.h"

namespace facebook::velox::connector::clp::search_lib {

/// A custom Velox VectorLoader that populates Velox vectors with serialized
/// JSON string from CLP column readers for the archive format.
class ClpArchiveJsonStringVectorLoader : public VectorLoader {
 public:
  ClpArchiveJsonStringVectorLoader(
      clp_s::SchemaReader* schemaReader,
      const std::shared_ptr<std::vector<uint64_t>>& filteredRowIndices);

 private:
  clp_s::SchemaReader* schemaReader_;
  std::shared_ptr<std::vector<uint64_t>> filteredRowIndices_;

  void loadInternal(
      RowSet rows,
      ValueHook* hook,
      vector_size_t resultSize,
      VectorPtr* result) override;
};

} // namespace facebook::velox::connector::clp::search_lib
