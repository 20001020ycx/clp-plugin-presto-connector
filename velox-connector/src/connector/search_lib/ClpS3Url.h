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

// Free of Velox, so `tests/` can build it outside the build-env image. See tests/README.md.

#pragma once

#include <string>
#include <string_view>

namespace facebook::velox::connector::clp::search_lib {

/// Drops one trailing '/' so that an endpoint joins onto a path cleanly.
///
/// @param endPoint
/// @return The endpoint without a trailing '/'.
inline std::string normalizeS3EndPoint(std::string_view endPoint) {
  if (false == endPoint.empty() && '/' == endPoint.back()) {
    endPoint.remove_suffix(1);
  }
  return std::string{endPoint};
}

/// Joins an endpoint, a bucket, and a split path into an S3 URL.
///
/// @param endPoint
/// @param bucket Empty when the bucket is already encoded in the endpoint (AWS virtual-hosted
/// style, e.g. https://bucket.s3.region.amazonaws.com) or in the split path.
/// @param splitPath
/// @return The constructed S3 URL.
inline std::string constructS3Url(
    std::string_view endPoint,
    std::string_view bucket,
    std::string_view splitPath) {
  std::string url{normalizeS3EndPoint(endPoint)};
  if (false == bucket.empty()) {
    url += '/';
    url += bucket;
  }
  url += '/';
  url += splitPath;
  return url;
}

} // namespace facebook::velox::connector::clp::search_lib
