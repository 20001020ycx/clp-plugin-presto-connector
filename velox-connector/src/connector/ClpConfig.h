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

namespace facebook::velox::config {
class ConfigBase;
} // namespace facebook::velox::config

namespace facebook::velox::connector::clp {

class ClpS3AuthProviderBase;

class ClpConfig {
 public:
  enum class S3AuthProvider {
    kClpPackage,
  };

  enum class StorageType {
    kFs,
    kS3,
  };

  static constexpr const char* kAuthProvider = "clp.s3-auth-provider";
  static constexpr const char* kStorageType = "clp.storage-type";

  explicit ClpConfig(std::shared_ptr<const config::ConfigBase> config);

  [[nodiscard]] const std::shared_ptr<const config::ConfigBase>& config()
      const {
    return config_;
  }

  StorageType storageType() const;
  std::shared_ptr<ClpS3AuthProviderBase> s3AuthProvider() const;

 private:
  std::shared_ptr<const config::ConfigBase> config_;
  std::shared_ptr<ClpS3AuthProviderBase> s3AuthProvider_;
  StorageType storageType_;
};

} // namespace facebook::velox::connector::clp
