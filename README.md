# Presto CLP connector plugins

## Requirements

* JDK 17
* [Task] >= 3.49.1

## Adding files

Certain file types need to be added to our linting rules manually:

* **YAML**. If adding a YAML file (regardless of its extension), add it (or its parent directory)
  as an argument to the `yamllint` command in [taskfiles/lint.yaml](taskfiles/lint.yaml).

## Linting

Before submitting a pull request, ensure you've run the linting commands below and either fixed any
violations or suppressed the warning.

To run all linting checks:

```shell
task lint:check
```

To run all linting checks AND automatically fix any fixable issues:

```shell
task lint:fix
```

### Running specific linters

The commands above run all linting checks, but for performance you may want to run a subset using
one of the tasks in the table below.

| Task               | Description                                          |
|--------------------|------------------------------------------------------|
| `lint:check-java`  | Runs the Java formatting check (Spotless).           |
| `lint:fix-java`    | Runs Spotless and fixes formatting issues.           |
| `lint:check-yaml`  | Runs the YAML linters.                               |
| `lint:fix-yaml`    | Runs the YAML linters and fixes issues.              |

## Testing

```shell
task presto-connector:test        # coordinator plugin, unit tests
task integration-tests:run        # both plugins, in a real Presto cluster
```

The worker plugin has no unit tests. [integration-tests/README.md](integration-tests/README.md)
explains why, and how a real cluster covers it instead.

## Building installable packages

For details about the `.deb`, `.rpm`, and `.tar.gz` artifacts built in CI, and to build them locally, see [tools/build-packages/README.md](tools/build-packages/README.md).

[Task]: https://taskfile.dev
