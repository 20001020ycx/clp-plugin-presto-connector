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

#include <boost/algorithm/string.hpp>

#include "connector/ClpConfig.h"
#include "connector/search_lib/ClpDefaultS3AuthProvider.h"
#include "connector/search_lib/ClpPackageS3AuthProvider.h"
#include "velox/common/base/Exceptions.h"
#include "velox/common/config/Config.h"

namespace facebook::velox::connector::clp {

namespace {

ClpConfig::S3AuthProvider stringToS3AuthProvider(const std::string& strValue) {
  auto upperValue = boost::algorithm::to_upper_copy(strValue);
  VELOX_CHECK(!upperValue.empty());
  if (upperValue == "CLP_PACKAGE") {
    return ClpConfig::S3AuthProvider::kClpPackage;
  }
  if (upperValue == "DEFAULT") {
    return ClpConfig::S3AuthProvider::kDefault;
  }
  VELOX_UNSUPPORTED("Unsupported s3 auth provider type: {}.", strValue);
}

ClpConfig::StorageType stringToStorageType(const std::string& strValue) {
  auto upperValue = boost::algorithm::to_upper_copy(strValue);
  if (upperValue == "FS") {
    return ClpConfig::StorageType::kFs;
  }
  if (upperValue == "S3") {
    return ClpConfig::StorageType::kS3;
  }
  VELOX_UNSUPPORTED("Unsupported storage type: {}.", strValue);
}

} // namespace

ClpConfig::ClpConfig(std::shared_ptr<const config::ConfigBase> config) {
  VELOX_CHECK_NOT_NULL(config, "Config is null for CLP initialization");
  config_ = std::move(config);

  storageType_ =
      stringToStorageType(config_->get<std::string>(kStorageType, "FS"));
  logSplitStats_ = config_->get<bool>(kLogSplitStats, false);

  if (StorageType::kS3 == storageType_) {
    // The provider authenticates this catalog's S3 requests with its own
    // credentials, without exporting them to the process's environment.
    switch (
        stringToS3AuthProvider(config_->get<std::string>(kAuthProvider, ""))) {
      case S3AuthProvider::kClpPackage:
        s3AuthProvider_ = std::make_shared<ClpPackageS3AuthProvider>(*config_);
        break;
      case S3AuthProvider::kDefault:
        s3AuthProvider_ = std::make_shared<ClpDefaultS3AuthProvider>(*config_);
        break;
      default:
        VELOX_FAIL();
    }
  }
}

std::shared_ptr<ClpS3AuthProviderBase> ClpConfig::s3AuthProvider() const {
  return s3AuthProvider_;
}

ClpConfig::StorageType ClpConfig::storageType() const {
  return storageType_;
}

bool ClpConfig::logSplitStats() const {
  return logSplitStats_;
}

} // namespace facebook::velox::connector::clp
