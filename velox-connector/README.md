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

## Testing

```shell
task velox-connector:test-with-installed-deps
```

This builds and runs the unit tests for the plugin's S3 authentication. They link CLP and the AWS
SDK but not Velox, which only the Presto worker provides. Like `build-with-installed-deps`, the
task uses the dependencies that `task velox-connector:deps:install-all` installs, so run it where
those are available, such as inside the build-env image (see
[tools/build-packages/README.md](../tools/build-packages/README.md)).

## Reading archives from S3

To read archives and IR streams from S3, set the following properties in the worker's catalog file.
Don't set them in the coordinator's catalog file: the coordinator plugin rejects properties that it
doesn't use.

* `clp.storage-type`: `s3`. The default, `fs`, reads splits from the worker's filesystem.
* `clp.s3-end-point`: The S3 endpoint, such as `https://s3.us-east-2.amazonaws.com`, or a
  virtual-hosted endpoint, such as `https://my-bucket.s3.us-east-2.amazonaws.com`. Requests are
  signed for the region in the endpoint's host name.
* `clp.s3-bucket`: The bucket that contains the splits. Omit it if the endpoint or the split paths
  already include the bucket.
* `clp.s3-auth-provider`: Where the worker gets the credentials that sign its requests:
  `clp_package` or `default`, described below.

### `clp_package`: an access key in the catalog

```properties
connector.name=clp
clp.storage-type=s3
clp.s3-auth-provider=clp_package
clp.s3-end-point=https://s3.us-east-2.amazonaws.com
clp.s3-bucket=my-bucket
clp.s3-access-key-id=<access-key-id>
clp.s3-secret-access-key=<secret-access-key>
```

`clp.s3-session-token` is optional. The worker never refreshes these credentials, so use `default`
for credentials that expire.

### `default`: the AWS SDK's default credential provider chain

```properties
connector.name=clp
clp.storage-type=s3
clp.s3-auth-provider=default
clp.s3-end-point=https://s3.us-east-2.amazonaws.com
clp.s3-bucket=my-bucket
```

The worker gets credentials from the AWS SDK for C++'s default credential provider chain, which
checks these sources in order:

1. The `AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`, and `AWS_SESSION_TOKEN` environment variables.
2. The shared config and credentials files.
3. Web identity federation, using `AWS_ROLE_ARN` and `AWS_WEB_IDENTITY_TOKEN_FILE`, which EKS sets
   for pods whose service account has an IAM role (IRSA).
4. SSO.
5. Container credentials, such as ECS task roles or EKS Pod Identity.
6. The EC2 instance profile, unless `AWS_EC2_METADATA_DISABLED` is `true`.

The SDK caches temporary credentials and refreshes them shortly before they expire, so the worker
keeps reading archives without a restart. The catalog can't also set `clp.s3-access-key-id`,
`clp.s3-secret-access-key`, or `clp.s3-session-token`.

For IRSA, the worker needs no credentials in its catalog or image. Its role needs `s3:GetObject` on
the archives. Since the chain checks environment variables and files first, make sure the worker
doesn't have static credentials there, and set `AWS_EC2_METADATA_DISABLED=true` if it mustn't fall
back to the node's instance profile.

### How requests are authenticated

Each catalog has its own credentials (and, with `default`, its own credential cache), and the worker
never copies them into its environment, so catalogs with different identities can share a worker.

The worker presigns the request for each split just before it sends it, using the catalog's current
credentials, so refreshed credentials apply to the next split. Each presigned URL is valid for 60
seconds and never outlives the credentials that signed it. If the credentials are unavailable,
incomplete, or expire within that time, the query fails with an error that contains no credentials
or URLs. The query also fails, instead of skipping the split, if S3 rejects the request (e.g.,
because the credentials lack `s3:GetObject`).

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
