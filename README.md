# Presto CLP connector plugins

## Requirements

* JDK 17
* [Maven] 3.8+
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

| Task                     | Description                                            |
|--------------------------|--------------------------------------------------------|
| `lint:check-yaml`        | Runs the YAML linters.                                 |
| `lint:fix-yaml`          | Runs the YAML linters and fixes issues.                |
| `lint:check-java-lint`   | Runs the Java linters with Spotless.                   |
| `lint:fix-java-lint`     | Runs the Java linters and fixes issues with Spotless.  |
| `lint:check-java-bug`    | Runs Java static analyzer with SpotBugs.               |

[Maven]: https://maven.apache.org/
[Task]: https://taskfile.dev
