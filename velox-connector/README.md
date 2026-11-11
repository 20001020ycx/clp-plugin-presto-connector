# Presto CLP Velox connector plugin

## Requirements

* [Task] >= 3.49.1
* One of the following build environments:
  * A machine with:
    * CMake >= 3.28.3 and < 4.0
    * A C++20 compiler
    * The following libraries:
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

## Compatibility with the Presto worker

The plugin must be built against the Presto sources that the worker loading it was built from. The
worker calls the plugin through C++ interfaces and doesn't check them when it loads the plugin, so
the plugin does: before it registers the connector, it compares the number of virtual functions
that the worker has for a connector interface with the number that the plugin was compiled with,
for each interface whose vtable the worker exports. If they differ, the worker fails to start, with
an error such as:

```text
The CLP connector plugin is incompatible with this presto_server: the plugin was built against
Presto 7d5072160258be09def72b18c92022843ee7e9b1, but presto_server was built from different sources
(the vtable of facebook::presto::PrestoToVeloxConnector has 12 entries in presto_server and 13 in
the plugin). Use a presto-native image built from that Presto commit, or a plugin built for this
presto_server.
```

The check doesn't detect other differences between the two builds, e.g., a virtual function with a
different signature, or a difference in an interface whose vtable the worker doesn't export.

[presto-native-dependency]: https://hub.docker.com/r/prestodb/presto-native-dependency
[Task]: https://taskfile.dev
[velox-deps-setup]: https://github.com/facebookincubator/velox#setting-up-dependencies
