# Presto CLP Velox connector plugin

## Requirements

* [Task] >= 3.49.1
* One of the following build environments:
  * A machine with:
    * CMake >= 3.28.3 and < 4.0
    * A C++20 compiler
    * A [Rust toolchain][rustup] (`cargo`), needed to build log-surgeon
    * The following libraries:
      * `libarchive-dev`
      * `libcurl4-openssl-dev`
      * `libevent-dev`
      * `libssl-dev`
  * A container with Velox's [dependencies][velox-deps-setup] installed, such as Presto's dev
    container ([prestodb/presto-native-dependency][presto-native-dependency]):
    * NOTE: Due to a bug in the container's GCC 12, log-surgeon won't compile; Instead, set
      `CC`/`CXX` to point at GCC 11 before building:

      ```shell
      export CC="gcc"
      export CXX="g++"
      ```

## Building

```shell
task velox-connector:build
```

Or with CMake directly:

```shell
task velox-connector:deps:install-all
cmake -S velox-connector -B build/velox-connector
cmake --build build/velox-connector -j
```

The built plugin will be at `build/velox-connector/libclp-plugin-velox-connector.so`.

[presto-native-dependency]: https://hub.docker.com/r/prestodb/presto-native-dependency
[rustup]: https://rustup.rs
[Task]: https://taskfile.dev
[velox-deps-setup]: https://github.com/facebookincubator/velox#setting-up-dependencies
