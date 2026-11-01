#!/usr/bin/env bash

# Builds the busybox init-container installer image from a connector package tarball.
#
# The image bundles both plugins (coordinator JAR + native worker .so and lib/) and, when
# run, installs each into a mounted target directory. See tools/build-packages/README.md.
#
# Reusable by local builds (build-packages.sh, --load) and CI (--push). Prints the built
# image reference to stdout.
#
# Requires: docker (with buildx), git, tar.

set -o errexit
set -o nounset
set -o pipefail

umask 0022

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
image_dir="${script_dir}/image"

# Shared helpers: resolve_image_repo (image repo derivation) and _REPO_ROOT.
source "${script_dir}/dependency-image/utils.sh"

show_help() {
    cat <<'EOF'
Usage: ./tools/build-packages/build-installer-init-image.sh --tarball FILE [OPTIONS]

Builds the busybox init-container installer image from a connector package tarball
(clp-plugin-presto-connector-<version>-linux-<arch>.tar.gz).

Options:
  --tarball FILE   Package tarball to build the image from (required)
  --version VER    Image version tag (default: parsed from the tarball name)
  --arch ARCH      amd64 or arm64 (default: parsed from the tarball name)
  --repo REPO      Image repository (default: derived from the git origin remote,
                    e.g. ghcr.io/y-scope/clp-plugin-presto-connector)
  --push           Push the image to the registry (default: --load into local docker)
  --load           Load the image into the local docker daemon (default)
  --digest-file F  Write the pushed image's registry digest (sha256:...) to F
                    (requires --push; used by CI to build the multi-arch manifest
                    from immutable digests instead of mutable per-arch tags)
  --help           Show this help

See tools/build-packages/README.md for details.
EOF
}

panic() {
    echo >&2 "ERROR: $*"
    exit 1
}

require_value() {
    [[ -n "${2:-}" ]] || panic "$1 requires a value"
}

# ── Parse arguments ───────────────────────────────────────────────────────────

tarball=""
version=""
arch=""
repo=""
output="--load"
digest_file=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --tarball) require_value "$1" "${2:-}"; tarball="$2"; shift 2 ;;
        --version) require_value "$1" "${2:-}"; version="$2"; shift 2 ;;
        --arch)    require_value "$1" "${2:-}"; arch="$2";    shift 2 ;;
        --repo)    require_value "$1" "${2:-}"; repo="$2";    shift 2 ;;
        --push)    output="--push"; shift ;;
        --load)    output="--load"; shift ;;
        --digest-file) require_value "$1" "${2:-}"; digest_file="$2"; shift 2 ;;
        --help)    show_help; exit 0 ;;
        *) panic "unknown option: $1 (use --help for usage)" ;;
    esac
done

[[ -n "${tarball}" ]] || panic "--tarball is required (use --help for usage)"
[[ -f "${tarball}" ]] || panic "tarball not found: ${tarball}"
[[ -z "${digest_file}" || "${output}" == "--push" ]] \
    || panic "--digest-file requires --push (only pushed images have a registry digest)"

command -v docker &>/dev/null || panic "docker is required"
docker buildx version &>/dev/null || panic "docker buildx is required"

# ── Resolve version and arch from the tarball name when not given ──────────────

# Tarball name format: clp-plugin-presto-connector-<version>-linux-<arch>.tar.gz
tar_base="$(basename "${tarball}")"
tar_base="${tar_base%.tar.gz}"
name_rest="${tar_base#clp-plugin-presto-connector-}"
if [[ "${name_rest}" == "${tar_base}" || "${name_rest}" != *-linux-* ]]; then
    panic "cannot parse tarball name '${tar_base}'; pass --version and --arch explicitly"
fi
[[ -n "${arch}" ]] || arch="${name_rest##*-linux-}"
[[ -n "${version}" ]] || version="${name_rest%-linux-"${arch}"}"

case "${arch}" in
    amd64) platform="linux/amd64" ;;
    arm64) platform="linux/arm64" ;;
    *) panic "unsupported arch: ${arch} (expected amd64 or arm64)" ;;
esac

[[ -n "${repo}" ]] || repo="$(resolve_image_repo)"

# Docker tags forbid '+' and '~'; the shared helper rejects versions image tags can't
# represent losslessly.
tag_version="$(package_version_to_image_tag "${version}")" \
    || panic "version '${version}' can't be used as an image tag" \
        "(only letters, digits, '.', '_', and '-' are allowed)"

# Pushed images get an arch suffix because the two CI legs need distinct registry names
# (the bare tag is the multi-arch manifest combining them). Local loads use the bare tag —
# the conventional Docker pattern where a locally-built image and the published one share
# a name and whatever is in the daemon wins.
if [[ "${output}" == "--push" ]]; then
    image="${repo}:${tag_version}-${arch}"
else
    image="${repo}:${tag_version}"
fi

# ── Assemble a self-contained build context and build ─────────────────────────

context_dir="$(mktemp -d)"
trap 'rm -rf "${context_dir}"' EXIT

# Extract the install tree so coordinator/ and worker/ sit at the context root, matching the
# Dockerfile's COPY paths. --strip-components=1 drops the versioned top-level directory.
tar -xzf "${tarball}" -C "${context_dir}" --strip-components=1
[[ -d "${context_dir}/coordinator" && -d "${context_dir}/worker" ]] \
    || panic "tarball did not contain coordinator/ and worker/ trees"

cp "${image_dir}/Dockerfile" "${image_dir}/entrypoint.sh" "${context_dir}/"

echo >&2 "==> Building installer image ${image} (${platform})..."
buildx_args=(
    --platform "${platform}"
    --tag "${image}"
    "${output}"
    -f "${context_dir}/Dockerfile"
)
metadata_file="${context_dir}/buildx-metadata.json"
[[ -z "${digest_file}" ]] || buildx_args+=(--metadata-file "${metadata_file}")
docker buildx build "${buildx_args[@]}" "${context_dir}"

if [[ -n "${digest_file}" ]]; then
    # Extract the registry digest from the buildx metadata JSON. sed keeps the dependency
    # footprint small (no jq); the strict format check makes a parse failure loud.
    digest="$(sed -n 's/.*"containerimage\.digest": *"\([^"]*\)".*/\1/p' "${metadata_file}")"
    [[ "${digest}" =~ ^sha256:[0-9a-f]{64}$ ]] \
        || panic "failed to extract image digest from buildx metadata"
    printf '%s\n' "${digest}" > "${digest_file}"
    echo >&2 "==> Pushed digest ${digest}"
fi

echo >&2 "==> Built ${image}"
echo "${image}"
