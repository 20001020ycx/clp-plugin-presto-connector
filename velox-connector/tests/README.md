# velox-connector unit tests

Unit tests for the parts of the connector that do not depend on Velox.

Run them with:

```shell
task velox-connector:test
```

## Why this is a separate CMake project

The plugin is built as a shared library that leaves Velox symbols undefined for the worker process
to resolve at load time, and Velox itself is never compiled here — it is fetched for its headers
only. A test binary linked against the connector would therefore have nothing to resolve those
symbols against.

Including a Velox header does not work either: `velox/type/Timestamp.h` reaches Folly, which is
only present inside the build-env image. So these tests are a standalone project that does not
include the plugin's `CMakeLists.txt`. They configure and run on any machine with a compiler,
needing neither `velox-connector:deps:install-all` nor the build-env image.

## What belongs here

A header is testable here if nothing that it includes reaches Velox. In practice that means no
`VELOX_*` macros, since each one constructs a Velox exception type, and no `config::ConfigBase`.

Today that leaves the standard library, but the rule is about Velox rather than third-party code
in general: another dependency is fine as long as this project can build it on its own, which for
now means fetching it here the way that gtest is fetched.

Keep the pure logic in such a header and let the Velox-facing caller wrap it: `ClpS3Url.h` holds
URL construction while `ClpPackageS3AuthProvider` keeps the `VELOX_CHECK` and the config reads.

There is nothing to remember, because the build enforces it: reaching for a Velox header from here
fails to compile, since this project does not add Velox to the include path.

Anything that touches `ClpDataSource`, splits, vectors, or cursors needs a running worker instead.
Those are covered by `integration-tests/`.
