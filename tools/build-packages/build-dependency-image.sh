#!/usr/bin/env bash

# Resolves this repo's dependency-image reference for local builds.
# Checks local Docker first, then the container registry, then builds a host-arch image.
#
# Prints the resolved image reference to stdout.
#
# Example:
#   image=$(tools/build-packages/build-dependency-image.sh)
#   docker run --rm -v "$(pwd):/src" -w /src "${image}" \
#       task velox-connector:build-with-installed-deps
#
# Requires: docker (with buildx), git, and sha256sum or shasum.

set -o errexit
set -o nounset
set -o pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
source "${script_dir}/dependency-image/utils.sh"

host_platform() {
    case "$(uname -m)" in
        x86_64) printf 'linux/amd64\n' ;;
        aarch64|arm64) printf 'linux/arm64\n' ;;
        *) echo >&2 "ERROR: unsupported host arch: $(uname -m)"; exit 1 ;;
    esac
}

main() {
    local build_env_hash image image_repo platform pull_err

    echo >&2 "==> Deriving build-env hash..."
    build_env_hash="$(derive_build_env_hash)"
    image_repo="$(resolve_image_repo)"
    image="$(image_ref "${image_repo}" "${build_env_hash}")"
    platform="$(host_platform)"

    echo >&2 "    build-env hash: ${build_env_hash}"
    echo >&2 "    image:          ${image}"

    if docker image inspect "${image}" &>/dev/null; then
        echo >&2 "==> Found in local Docker cache."
        echo "${image}"
        return
    fi

    echo >&2 "==> Checking repository registry..."
    if pull_err="$(docker pull "${image}" 2>&1)"; then
        echo >&2 "==> Pulled from repository registry."
        echo "${image}"
        return
    fi

    echo >&2 "    docker pull failed; will build from scratch. Pull error:"
    printf '%s\n' "${pull_err}" | sed 's/^/      /' >&2

    echo >&2 "==> Image not available — building from scratch..."
    build_image "${image}" "${platform}" "--load"
    echo >&2 "==> Built locally."
    echo "${image}"
}

main
