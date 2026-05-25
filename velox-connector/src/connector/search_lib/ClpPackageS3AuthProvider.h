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

#include "connector/search_lib/ClpS3AuthProviderBase.h"

namespace facebook::velox::config {
class ConfigBase;
} // namespace facebook::velox::config

namespace facebook::velox::connector::clp {

class ClpPackageS3AuthProvider : public ClpS3AuthProviderBase {
 public:
  explicit ClpPackageS3AuthProvider(
      std::shared_ptr<const config::ConfigBase> config)
      : ClpS3AuthProviderBase(config) {}

  static constexpr const char* kAccessKeyId = "clp.s3-access-key-id";
  static constexpr const char* kBucket = "clp.s3-bucket";
  static constexpr const char* kEndPoint = "clp.s3-end-point";
  static constexpr const char* kSecretAccessKey = "clp.s3-secret-access-key";
  static constexpr const char* kSessionToken = "clp.s3-session-token";

  static constexpr const char* kEnvAwsAccessKeyId = "AWS_ACCESS_KEY_ID";
  static constexpr const char* kEnvAwsSecretAccessKey = "AWS_SECRET_ACCESS_KEY";
  static constexpr const char* kEnvAwsSessionToken = "AWS_SESSION_TOKEN";

  std::string constructS3Url(std::string_view splitPath) override;

  bool exportAuthEnvironmentVariables() override;

 private:
  std::string bucket_;
  std::string endPoint_;
};

} // namespace facebook::velox::connector::clp
