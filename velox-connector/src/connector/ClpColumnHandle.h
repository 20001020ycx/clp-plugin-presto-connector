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

#include "velox/connectors/Connector.h"

namespace facebook::velox::connector::clp {

class ClpColumnHandle : public ColumnHandle {
 public:
  ClpColumnHandle(
      const std::string& columnName,
      const std::string& originalColumnName,
      const TypePtr& columnType)
      : columnName_(columnName),
        originalColumnName_(originalColumnName),
        columnType_(columnType) {}

  const std::string& name() const override {
    return columnName_;
  }

  const std::string& columnName() const {
    return columnName_;
  }

  const std::string& originalColumnName() const {
    return originalColumnName_;
  }

  const TypePtr& columnType() const {
    return columnType_;
  }

  inline static const std::string jsonStringColumnName_ = "__json_string";

 private:
  const std::string columnName_;
  const std::string originalColumnName_;
  const TypePtr columnType_;
};

} // namespace facebook::velox::connector::clp
