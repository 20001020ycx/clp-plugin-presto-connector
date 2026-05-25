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

#include "connector/search_lib/ClpPackageS3AuthProvider.h"
#include "velox/common/base/Exceptions.h"
#include "velox/common/config/Config.h"

namespace facebook::velox::connector::clp {

std::string ClpPackageS3AuthProvider::constructS3Url(
    std::string_view splitPath) {
  VELOX_CHECK(!splitPath.empty(), "splitPath cannot be empty");
  // For URLs where the bucket is already encoded in the endpoint (e.g., AWS S3
  // virtual-hosted style: https://bucket.s3.region.amazonaws.com).
  if (bucket_.empty()) {
    return fmt::format("{}/{}", endPoint_, splitPath);
  }
  return fmt::format("{}/{}/{}", endPoint_, bucket_, splitPath);
}

bool ClpPackageS3AuthProvider::exportAuthEnvironmentVariables() {
  endPoint_ = config_->get<std::string>(kEndPoint, "");
  VELOX_CHECK(!endPoint_.empty(), fmt::format("{} cannot be empty", kEndPoint));
  if ('/' == endPoint_.back()) {
    endPoint_.pop_back();
  }

  bucket_ = config_->get<std::string>(kBucket, "");

  auto accessKeyId = config_->get<std::string>(kAccessKeyId, "");
  auto secretAccessKey = config_->get<std::string>(kSecretAccessKey, "");
  auto sessionToken = config_->get<std::string>(kSessionToken, "");
  VELOX_CHECK(
      !accessKeyId.empty(), fmt::format("{} cannot be empty", kAccessKeyId));
  VELOX_CHECK(
      !secretAccessKey.empty(),
      fmt::format("{} cannot be empty", kSecretAccessKey));

  setEnvironmentVariable(kEnvAwsAccessKeyId, accessKeyId);
  setEnvironmentVariable(kEnvAwsSecretAccessKey, secretAccessKey);

  if (!sessionToken.empty()) {
    setEnvironmentVariable(kEnvAwsSessionToken, sessionToken);
  } else {
    unsetEnvironmentVariable(kEnvAwsSessionToken);
  }

  return true;
}

} // namespace facebook::velox::connector::clp
