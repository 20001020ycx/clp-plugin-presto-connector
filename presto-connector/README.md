# Presto CLP coordinator connector plugin

## Requirements

* JDK 17
* [Maven] 3.8+
* [Task] >= 3.49.1

## Building

```shell
task presto-connector:build
```

Or with Maven directly:

```shell
cd presto-connector && mvn package -DskipTests
```

## Testing

```shell
task presto-connector:test
```

[Maven]: https://maven.apache.org/
[Task]: https://taskfile.dev
