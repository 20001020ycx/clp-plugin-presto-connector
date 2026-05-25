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

#include <cstdlib>
#include <unordered_map>

#include "gtest/gtest.h"
#include "velox/common/base/tests/GTestUtils.h"
#include "velox/common/config/Config.h"
#include "connector/ClpConfig.h"
#include "connector/search_lib/ClpPackageS3AuthProvider.h"

namespace facebook::velox::connector::clp {

namespace {

class ClpEnvironmentVariableGuard {
 public:
  void saveOriginalEnvironmentVariables() {
    originalEnv_.clear();
    originalKeys_.clear();

#if defined(_WIN32)
    LPCH envStrings = GetEnvironmentStringsA();
    if (!envStrings)
      return;

    LPCH var = envStrings;
    while (*var) {
      std::string entry(var);
      auto pos = entry.find('=');
      if (pos != std::string::npos) {
        std::string key = entry.substr(0, pos);
        std::string value = entry.substr(pos + 1);
        originalEnv_[key] = value;
        originalKeys_.insert(key);
      }
      var += entry.size() + 1;
    }
    FreeEnvironmentStringsA(envStrings);
#elif defined(__unix__) || defined(__APPLE__)
    for (char** current = environ; *current; ++current) {
      std::string entry(*current);
      auto pos = entry.find('=');
      if (pos != std::string::npos) {
        std::string key = entry.substr(0, pos);
        std::string value = entry.substr(pos + 1);
        originalEnv_[key] = value;
        originalKeys_.insert(key);
      }
    }
#else
    VELOX_UNSUPPORTED("Unsupported OS");
#endif
  }

  void restoreEnvironmentVariables() {
#if defined(_WIN32)
    // Remove added vars and restore originals
    for (auto const& [key, value] : originalEnv_) {
      _putenv_s(key.c_str(), value.c_str()); // restore original
    }

    // Collect variables to unset
    std::vector<std::string> keysToUnset;
    for (char** env = _environ; *env; ++env) {
      std::string entry(*env);
      auto pos = entry.find('=');
      if (pos != std::string::npos) {
        std::string key = entry.substr(0, pos);
        if (originalKeys_.find(key) == originalKeys_.end()) {
          keysToUnset.push_back(key);
        }
      }
    }

    // Unset them
    for (const auto& key : keysToUnset) {
      _putenv((key + "=").c_str());
    }
#elif defined(__unix__) || defined(__APPLE__)
    // Restore original variables
    for (auto const& [key, value] : originalEnv_) {
      setenv(key.c_str(), value.c_str(), 1);
    }

    // Collect keys to unset
    std::vector<std::string> keysToUnset;
    for (char** env = environ; *env; ++env) {
      std::string entry(*env);
      auto pos = entry.find('=');
      if (pos != std::string::npos) {
        std::string key = entry.substr(0, pos);
        if (originalKeys_.find(key) == originalKeys_.end()) {
          keysToUnset.push_back(key);
        }
      }
    }

    // Unset them
    for (const auto& key : keysToUnset) {
      unsetenv(key.c_str());
    }
#else
    VELOX_UNSUPPORTED("Unsupported OS");
#endif
  }

 private:
  std::map<std::string, std::string> originalEnv_;
  std::set<std::string> originalKeys_;
};

class ClpConfigTest : public testing::Test {
 public:
  explicit ClpConfigTest()
      : envVarGuard_(std::make_unique<ClpEnvironmentVariableGuard>()) {}

  void SetUp() override {
    envVarGuard_->saveOriginalEnvironmentVariables();
  }

  void TearDown() override {
    envVarGuard_->restoreEnvironmentVariables();
  }

  std::unique_ptr<ClpConfig> buildClpConfig(
      std::unordered_map<std::string, std::string> configMap) {
    auto config =
        std::make_shared<const config::ConfigBase>(std::move(configMap));
    return std::make_unique<ClpConfig>(config);
  }

 private:
  std::unique_ptr<ClpEnvironmentVariableGuard> envVarGuard_;
};

class ClpS3AuthProviderBaseTest : public ClpConfigTest {
 public:
  /// Checks whether an environment variable matches a given value.
  ///
  /// @param key The name of the environment variable to check.
  /// @param expectedValue Optional expected value to compare against. If
  ///        std::nullopt, the function returns true if the variable is not
  ///        defined.
  /// @return True if:
  ///         - expectedValue is std::nullopt and the variable is not defined,
  ///         or
  ///         - expectedValue is set and matches the variable's current value.
  bool checkEnvironmentVariableEquals(
      std::string_view key,
      std::optional<std::string_view> expectedValue) {
    const char* actualValue = std::getenv(std::string(key).c_str());
    if (nullptr == actualValue) {
      return !expectedValue.has_value();
    }
    return expectedValue.has_value() &&
        std::string_view(actualValue) == expectedValue.value();
  }
};

class ClpPackageS3AuthProviderTest : public ClpS3AuthProviderBaseTest {
 public:
  std::unique_ptr<ClpPackageS3AuthProvider> buildClpPackageS3AuthProvider(
      std::unordered_map<std::string, std::string> configMap) {
    auto config =
        std::make_shared<const config::ConfigBase>(std::move(configMap));
    return std::make_unique<ClpPackageS3AuthProvider>(config);
  }
};

} // namespace

TEST_F(ClpConfigTest, invalidAuthProvider) {
  const std::unordered_map<std::string, std::string> configMap(
      {{"clp.storage-type", "s3"},
       {ClpConfig::kAuthProvider, "dummy-provider"}});
  VELOX_ASSERT_UNSUPPORTED_THROW(
      buildClpConfig(configMap),
      "Unsupported s3 auth provider type: dummy-provider.");
}

TEST_F(ClpS3AuthProviderBaseTest, caseInsensitiveAuthProvider) {
  const std::unordered_map<std::string, std::string> configMap(
      {{"clp.storage-type", "s3"},
       {ClpConfig::kAuthProvider, "ClP_PaCkAgE"},
       {ClpPackageS3AuthProvider::kAccessKeyId, "aaaaaa"},
       {ClpPackageS3AuthProvider::kEndPoint, "http://aaaaaa"},
       {ClpPackageS3AuthProvider::kSecretAccessKey, "cccccc"}});
  VELOX_CHECK_NOT_NULL(buildClpConfig(configMap));
}

TEST_F(ClpPackageS3AuthProviderTest, readAndExportAwsAuthEnvironmentVariables) {
  const std::string cTestAccessKeyId{"aaaaaa"};
  const std::string cTestBucket{"test-bucket"};
  const std::string cTestEndPoint{"http://localhost:9000"};
  const std::string cTestSecretAccessKey{"bbbbbb"};
  const std::string cTestSessionToken{"cccccc"};

  // Test all properties
  std::unordered_map<std::string, std::string> configMap(
      {{"clp.storage-type", "s3"},
       {ClpConfig::kAuthProvider, "clp_package"},
       {ClpPackageS3AuthProvider::kAccessKeyId, cTestAccessKeyId},
       {ClpPackageS3AuthProvider::kBucket, cTestBucket},
       {ClpPackageS3AuthProvider::kEndPoint, cTestEndPoint},
       {ClpPackageS3AuthProvider::kSecretAccessKey, cTestSecretAccessKey},
       {ClpPackageS3AuthProvider::kSessionToken, cTestSessionToken}});
  auto clpPackageS3AuthProvider = buildClpPackageS3AuthProvider(configMap);
  VELOX_CHECK(clpPackageS3AuthProvider->exportAuthEnvironmentVariables());
  VELOX_CHECK(checkEnvironmentVariableEquals(
      ClpPackageS3AuthProvider::kEnvAwsAccessKeyId, cTestAccessKeyId));
  VELOX_CHECK(checkEnvironmentVariableEquals(
      ClpPackageS3AuthProvider::kEnvAwsSecretAccessKey, cTestSecretAccessKey));
  VELOX_CHECK(checkEnvironmentVariableEquals(
      ClpPackageS3AuthProvider::kEnvAwsSessionToken, cTestSessionToken));

  // Test auth without the session token
  configMap = {
      {"clp.storage-type", "s3"},
      {ClpConfig::kAuthProvider, "clp_package"},
      {ClpPackageS3AuthProvider::kAccessKeyId, cTestAccessKeyId},
      {ClpPackageS3AuthProvider::kEndPoint, cTestEndPoint},
      {ClpPackageS3AuthProvider::kSecretAccessKey, cTestSecretAccessKey}};
  clpPackageS3AuthProvider = buildClpPackageS3AuthProvider(configMap);
  VELOX_CHECK(clpPackageS3AuthProvider->exportAuthEnvironmentVariables());
  VELOX_CHECK(checkEnvironmentVariableEquals(
      ClpPackageS3AuthProvider::kEnvAwsSessionToken, std::nullopt));
}

// Tests URL construction for S3-compatible storage (e.g., MinIO) using
// path-style URLs with bucket.
TEST_F(ClpPackageS3AuthProviderTest, constructS3UrlForPathStyleWithBucket) {
  const std::string cTestAccessKeyId{"aaaaaa"};
  const std::string cTestBucket{"logs"};
  const std::string cTestEndPoint{"http://172.26.105.44:9000"};
  const std::string cTestSecretAccessKey{"bbbbbb"};
  const std::string cTestSplitPath{"archives/default/abc123"};

  std::unordered_map<std::string, std::string> configMap(
      {{"clp.storage-type", "s3"},
       {ClpConfig::kAuthProvider, "clp_package"},
       {ClpPackageS3AuthProvider::kAccessKeyId, cTestAccessKeyId},
       {ClpPackageS3AuthProvider::kBucket, cTestBucket},
       {ClpPackageS3AuthProvider::kEndPoint, cTestEndPoint},
       {ClpPackageS3AuthProvider::kSecretAccessKey, cTestSecretAccessKey}});
  auto clpPackageS3AuthProvider = buildClpPackageS3AuthProvider(configMap);
  VELOX_CHECK(clpPackageS3AuthProvider->exportAuthEnvironmentVariables());

  auto url = clpPackageS3AuthProvider->constructS3Url(cTestSplitPath);
  VELOX_CHECK_EQ(url, "http://172.26.105.44:9000/logs/archives/default/abc123");
}

// Tests URL construction for AWS S3 using virtual-hosted style URLs without
// bucket config.
TEST_F(ClpPackageS3AuthProviderTest, constructS3UrlForAwsVirtualHostedStyle) {
  const std::string cTestAccessKeyId{"aaaaaa"};
  const std::string cTestEndPoint{"https://logs.s3.us-east-1.amazonaws.com"};
  const std::string cTestSecretAccessKey{"bbbbbb"};
  const std::string cTestSplitPath{"archives/default/abc123"};

  std::unordered_map<std::string, std::string> configMap(
      {{"clp.storage-type", "s3"},
       {ClpConfig::kAuthProvider, "clp_package"},
       {ClpPackageS3AuthProvider::kAccessKeyId, cTestAccessKeyId},
       {ClpPackageS3AuthProvider::kEndPoint, cTestEndPoint},
       {ClpPackageS3AuthProvider::kSecretAccessKey, cTestSecretAccessKey}});
  auto clpPackageS3AuthProvider = buildClpPackageS3AuthProvider(configMap);
  VELOX_CHECK(clpPackageS3AuthProvider->exportAuthEnvironmentVariables());

  auto url = clpPackageS3AuthProvider->constructS3Url(cTestSplitPath);
  VELOX_CHECK_EQ(
      url, "https://logs.s3.us-east-1.amazonaws.com/archives/default/abc123");
}

// Tests URL construction for AWS S3 using path-style URLs with bucket config.
TEST_F(ClpPackageS3AuthProviderTest, constructS3UrlForAwsPathStyleWithBucket) {
  const std::string cTestAccessKeyId{"aaaaaa"};
  const std::string cTestBucket{"logs"};
  const std::string cTestEndPoint{"https://s3.us-east-1.amazonaws.com"};
  const std::string cTestSecretAccessKey{"bbbbbb"};
  const std::string cTestSplitPath{"archives/default/abc123"};

  std::unordered_map<std::string, std::string> configMap(
      {{"clp.storage-type", "s3"},
       {ClpConfig::kAuthProvider, "clp_package"},
       {ClpPackageS3AuthProvider::kAccessKeyId, cTestAccessKeyId},
       {ClpPackageS3AuthProvider::kBucket, cTestBucket},
       {ClpPackageS3AuthProvider::kEndPoint, cTestEndPoint},
       {ClpPackageS3AuthProvider::kSecretAccessKey, cTestSecretAccessKey}});
  auto clpPackageS3AuthProvider = buildClpPackageS3AuthProvider(configMap);
  VELOX_CHECK(clpPackageS3AuthProvider->exportAuthEnvironmentVariables());

  auto url = clpPackageS3AuthProvider->constructS3Url(cTestSplitPath);
  VELOX_CHECK_EQ(
      url, "https://s3.us-east-1.amazonaws.com/logs/archives/default/abc123");
}

} // namespace facebook::velox::connector::clp
