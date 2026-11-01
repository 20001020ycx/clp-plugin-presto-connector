# CLP Presto connector packaging

This directory builds installable `.deb`, `.rpm`, and `.tar.gz` artifacts, plus a busybox
init-container installer image, for the CLP Presto connector (coordinator + worker) on
`amd64` and `arm64`.

CI packaging runs `tools/build-packages/internal/container/build-artifacts.sh`
through `.github/workflows/build-packages.yaml`. Local builds use
`build-packages.sh`, which resolves the build-env image and invokes the same
container-side script.

Supported package version format: must start with a digit and use only
`[0-9A-Za-z.+~-]`. The installer image additionally rejects versions containing
`+` or `~` (Docker tags can't represent them).

For command options, run `--help` on the relevant entry point.

Default outputs are written to `./packages`.
`coordinator/` contains `clp-plugin-presto-connector.jar`; `worker/` contains
`libclp-plugin-velox-connector.so` and bundled non-system runtime `.so` files.

## Local usage

```bash
task package
```

A thin wrapper over `./tools/build-packages/build-packages.sh` (call that directly if `go-task` isn't installed). The script accepts `--output DIR`, `--version VER`, and `--with-ca-certs`. The task takes each as a variable instead, so that a task that depends on this one does not forward its own arguments here:

```bash
task package BUILD_PACKAGE_OUTPUT=DIR BUILD_PACKAGE_VERSION=VER BUILD_PACKAGE_WITH_CA_CERTS=1
```

Each variable may also be set in the environment.

### Installer image

`task package` also builds and loads a busybox init-container image that bundles both plugins. Its entrypoint copies each component into a mounted volume named by `COORDINATOR_PLUGIN_INSTALL_PATH` / `WORKER_PLUGIN_INSTALL_PATH` (set either or both):

```bash
docker run --rm -e WORKER_PLUGIN_INSTALL_PATH=/plugins -v "$(pwd)/plugins:/plugins" \
  ghcr.io/y-scope/clp-plugin-presto-connector:<version>
```

Run `./tools/build-packages/build-installer-init-image.sh --help` to build it standalone from any package tarball.

In CI, `build-packages.yaml` builds the image per architecture on every run and
combines them into a multi-arch `:<version>` tag; pushes to GHCR happen only
from the default branch and version tags. Local builds load the same
`:<version>` tag (single-arch, for the build host) — a locally-built image
therefore shadows the published one in your Docker daemon until you
`docker pull` it.

The build runs inside a hash-tagged **build-env image** (`env-<hash>`) based on
`manylinux_2_28`. `build-dependency-image.sh` resolves it from the local Docker
cache, the container registry, or a local build, reusing the cached image on
later runs.

Images live under `ghcr.io/<owner>/<repo>` by default, derived from the origin
remote. `CI_CONTAINER_REGISTRY` and `CI_CONTAINER_REGISTRY_REPO` override the
host and the path below it, and CI reads them from repository variables
alongside the `CI_CONTAINER_REGISTRY_USERNAME` / `CI_CONTAINER_REGISTRY_PASSWORD`
secrets. Any registry other than GHCR requires the two secrets: the GitHub token
is used only while `CI_CONTAINER_REGISTRY` is unset or set to `ghcr.io`, so that a
partial configuration fails instead of sending that token to another host. Set both
to the same values CI uses when building locally against a private registry, and
`docker login` to it first: an unauthenticated pull is not an error, it just falls
back to rebuilding the image from scratch.

Both images share that one repository and are told apart by tag: the installer
uses `:<version>`, and the build-env image uses `:env-<hash>` (per-architecture
builds add `-<arch>`). The build-env image is not a sub-repository such as
`<repo>/build-env` because Docker Hub only accepts `<namespace>/<repository>`.
For Docker Hub, set the registry to `docker.io` and the repository to
`<namespace>/<name>`:

```bash
CI_CONTAINER_REGISTRY=docker.io
CI_CONTAINER_REGISTRY_REPO=myorg/clp-plugin-presto-connector
```

That publishes `docker.io/myorg/clp-plugin-presto-connector:<version>` and
`docker.io/myorg/clp-plugin-presto-connector:env-<hash>`. Build-env images from
before this layout sat at `<repo>/build-env:env-<hash>` and are no longer read,
so the first build afterwards rebuilds the image once; the old tags can be
deleted.

Build state is cached under `.cache/` (`maven/`, `ccache/`,
`fetchcontent/<hash>/`, and `build/<hash>/` for persisted CMake/build state),
shared across build-env revisions. `.cache/build/<hash>/` is the container's
build output directory and is distinct from the repository's separate top-level
`build/` directory used by non-container local dev builds. The local wrapper
runs the container with the invoking host UID/GID, so it does not create
root-owned files; any root-owned files in `.cache/`, `build/`, or
`presto-connector/target/` are leftovers from earlier privileged or CI builds,
while `packages/` is owned by the invoking user.

### Prerequisites

Docker with buildx (usable without `sudo`), git, `tar`, `sha256sum` or `shasum`, and
~10 GB free disk for the build-env image.

## Target-CPU flags

The worker plugin must be built with the same target-CPU flags as the Presto
worker that loads it; the defaults match the official presto-native images. To
target a worker built with different flags, set the `CPU_TARGET` environment
variable (on arm, `ARM_BUILD_TARGET` is a second knob) — both are forwarded
into the packaging container:

```bash
CPU_TARGET=sse task package
```

In CI, triggering `build-packages.yaml` manually (workflow dispatch) exposes
per-architecture inputs — `amd64_cpu_target`, `arm64_cpu_target`, and
`arm64_build_target` — each applied only to the matching architecture's build,
so an input can never affect the other architecture. Blank inputs — and
push-triggered builds — use the official presto-native defaults.

See `velox-connector/cmake/DeriveTargetCpuFlags.cmake` for the accepted
keywords, defaults, and how to find the right value for a given worker.
