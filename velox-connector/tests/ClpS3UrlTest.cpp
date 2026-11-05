/*
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

#include "connector/search_lib/ClpS3Url.h"

#include <gtest/gtest.h>

namespace facebook::velox::connector::clp::search_lib {
namespace {

constexpr const char* kSplitPath = "archives/default/abc123";

// An S3-compatible store (e.g. MinIO) addressed path-style, with the bucket configured.
TEST(ClpS3UrlTest, pathStyleWithBucket) {
  EXPECT_EQ(
      constructS3Url("http://172.26.105.44:9000", "logs", kSplitPath),
      "http://172.26.105.44:9000/logs/archives/default/abc123");
}

// AWS virtual-hosted style, where the bucket is already part of the endpoint.
TEST(ClpS3UrlTest, awsVirtualHostedStyleWithoutBucket) {
  EXPECT_EQ(
      constructS3Url("https://logs.s3.us-east-1.amazonaws.com", "", kSplitPath),
      "https://logs.s3.us-east-1.amazonaws.com/archives/default/abc123");
}

TEST(ClpS3UrlTest, awsPathStyleWithBucket) {
  EXPECT_EQ(
      constructS3Url("https://s3.us-east-1.amazonaws.com", "logs", kSplitPath),
      "https://s3.us-east-1.amazonaws.com/logs/archives/default/abc123");
}

// The integration harness leaves the bucket unset and lets the split path carry it.
TEST(ClpS3UrlTest, bucketCarriedBySplitPath) {
  EXPECT_EQ(
      constructS3Url("http://minio:9000", "", "fixtures/http_logs/archive"),
      "http://minio:9000/fixtures/http_logs/archive");
}

TEST(ClpS3UrlTest, trailingSlashOnEndPointIsNotDoubled) {
  EXPECT_EQ(constructS3Url("http://minio:9000/", "logs", "a"), "http://minio:9000/logs/a");
  EXPECT_EQ(constructS3Url("http://minio:9000/", "", "a"), "http://minio:9000/a");
}

TEST(ClpS3UrlTest, normalizeEndPointDropsAtMostOneSlash) {
  EXPECT_EQ(normalizeS3EndPoint("http://minio:9000"), "http://minio:9000");
  EXPECT_EQ(normalizeS3EndPoint("http://minio:9000/"), "http://minio:9000");
  EXPECT_EQ(normalizeS3EndPoint("http://minio:9000//"), "http://minio:9000/");
  EXPECT_EQ(normalizeS3EndPoint(""), "");
}

} // namespace
} // namespace facebook::velox::connector::clp::search_lib
