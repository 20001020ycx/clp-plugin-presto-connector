# Velox Connector


## Requirements

* CMake 3.28+
* C++20 compatible compiler
* [Task] 3.38.0+

## Building

```shell
task velox-connector:build
```

Or with CMake directly:

```shell
task deps:install-all
cmake -S . -B build/velox-connector
cmake --build build/velox-connector -j
```

The built plugin is at `build/velox-connector/libclp-plugin-velox-connector.so`.

[Task]: https://taskfile.dev
