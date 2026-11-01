#!/usr/bin/env bash

# Shared dependency-image helpers for tag derivation and Docker builds.
# Callers decide whether to use a local image, pull from the registry, or build.

_REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." &>/dev/null && pwd)"

# Keep local builds working when the checkout lacks initialized submodules.
ensure_yscope_dev_utils_submodule() {
    git -C "${_REPO_ROOT}" submodule update --init --recursive tools/yscope-dev-utils
}

# ── Image identity ────────────────────────────────────────────────────────────

# Builds the content-addressed build-env image reference.
#
# The image kind goes in the tag rather than a path component: Docker Hub only accepts
# `<namespace>/<repository>`, so a `<repo>/build-env` path would be unpublishable there. The two
# kinds can share a repository because version tags start with a digit (see
# `package_version_to_image_tag`) while these start with `env-`.
#
# Args: <repo> <build-env-hash>
image_ref() {
    echo "$1:env-$2"
}

# Strips leading and trailing slashes so a pasted value joins cleanly.
_trim_slashes() {
    local value="${1}"
    while [[ "${value}" == /* ]]; do
        value="${value#/}"
    done
    while [[ "${value}" == */ ]]; do
        value="${value%/}"
    done
    printf '%s' "${value}"
}

# Resolves the image repo that every image reference is built under.
#
# `CI_CONTAINER_REGISTRY` and `CI_CONTAINER_REGISTRY_REPO` override the host and the path below it,
# so that a deployment can publish somewhere other than GHCR. Both default to what this repo has
# always used, which keeps forks and local builds working without any configuration. CI and a local
# build must agree here: the build-env image is content-addressed, so a mismatch turns a pull into
# a rebuild rather than an error.
resolve_image_repo() {
    local registry repo remote_url
    # A scheme is valid for `docker login` but not in an image reference, so accept the value
    # someone is most likely to paste: a failed pull is not an error, it silently rebuilds.
    registry="${CI_CONTAINER_REGISTRY:-ghcr.io}"
    registry="${registry#*://}"
    registry="$(_trim_slashes "${registry}")"
    repo="$(_trim_slashes "${CI_CONTAINER_REGISTRY_REPO:-}")"
    if [[ -z "${repo}" ]]; then
        remote_url="$(git -C "${_REPO_ROOT}" remote get-url origin)"
        case "${remote_url}" in
            https://github.com/*) repo="${remote_url#https://github.com/}" ;;
            git@github.com:*) repo="${remote_url#git@github.com:}" ;;
            ssh://git@github.com/*) repo="${remote_url#ssh://git@github.com/}" ;;
            *)
                echo >&2 "ERROR: can't derive an image repo from origin remote: ${remote_url}"
                echo >&2 "       Expected a github.com remote, or set CI_CONTAINER_REGISTRY_REPO."
                exit 1
                ;;
        esac
        repo="${repo%.git}"
    fi
    printf '%s/%s\n' "${registry}" "$(printf '%s' "${repo}" | tr '[:upper:]' '[:lower:]')"
}

# Validates a package version for use as a Docker tag and prints it unchanged. Docker tags
# allow only [A-Za-z0-9_.-], so versions containing '+' or '~' (valid in packages) are
# rejected rather than encoded — a lossy substitution would collide distinct versions
# (e.g. '1.0+rc' and '1.0~rc'), and an encoding would produce unreadable tags. The single
# shared definition keeps local builds and the CI manifest job producing identical tags.
#
# Args: <version>
# Fails when the version isn't a digit followed by letters, digits, '.', '_', or '-'.
package_version_to_image_tag() {
    local version="$1"
    [[ "${version}" =~ ^[0-9][0-9A-Za-z._-]*$ ]] || return 1
    printf '%s\n' "${version}"
}

# Inputs that should change the build-env image tag.
_BUILD_ENV_HASH_INPUTS=(
    ".dockerignore"
    ".github/workflows/build-dependency-image.yaml"
    "taskfile.yaml"
    "taskfiles"
    "tools/build-packages/build-dependency-image.sh"
    "tools/build-packages/dependency-image"
    "tools/yscope-dev-utils"
)

# Computes the 16-hex-char hash used in the image tag.
#
# Requires: git, and either sha256sum (Linux) or shasum -a 256 (macOS).
derive_build_env_hash() {
    (
        cd "${_REPO_ROOT}" || exit
        ensure_yscope_dev_utils_submodule >&2

        # macOS ships `shasum` rather than `sha256sum`; pick whichever exists.
        # Both emit the same `<hash>  <file>` format, so the pipeline is unchanged.
        local sha256_cmd=(sha256sum)
        if ! command -v sha256sum &>/dev/null; then
            sha256_cmd=(shasum -a 256)
        fi

        git ls-files -z --recurse-submodules -- "${_BUILD_ENV_HASH_INPUTS[@]}" \
            | LC_ALL=C sort -z  \
            | xargs -0 "${sha256_cmd[@]}" \
            | "${sha256_cmd[@]}" \
            | cut -c1-16
    )
}

# Stages the host CA bundle into the temporary Docker build context.
#
# Args: <destination-path>
_stage_host_ca_bundle() {
    local dest="${1:?_stage_host_ca_bundle requires a destination path}"
    local ca_bundle_candidates=(
        "${SSL_CERT_FILE:-}"
        /etc/ssl/certs/ca-certificates.crt
        /etc/pki/tls/certs/ca-bundle.crt
        /etc/ssl/cert.pem
    )

    local src
    for src in "${ca_bundle_candidates[@]}"; do
        [[ -f "${src}" && -s "${src}" ]] || continue
        echo >&2 "==> Staging host CA bundle: ${src} -> ${dest}"
        if ! cp "${src}" "${dest}"; then
            echo >&2 "ERROR: failed to stage host CA bundle: ${src}"
            return 1
        fi
        return 0
    done

    echo >&2 "==> No host CA bundle found; continuing without host CA context."
    return 1
}

# ── Docker build ──────────────────────────────────────────────────────────────

# Builds the dependency image.
#
# Args:
#   $1  image tag    — e.g. ghcr.io/owner/repo:env-<hash>
#   $2  platform     — linux/amd64 or linux/arm64
#   $3  output flag  — --push (registry) or --load (local docker)
#
# Requires: docker buildx, git
build_image() {
    local tag="$1" platform="$2" output="$3"

    # Expose the host CA bundle as a narrow named build context so the Dockerfile
    # can bind-mount it during networked RUN steps without baking it into image
    # layers. Use a real context instead of a BuildKit secret because corporate CA
    # bundles can exceed BuildKit's 500KiB secret limit.
    local ca_stage; ca_stage=$(mktemp -d)
    (
        trap 'rm -rf "${ca_stage}"' EXIT

        local ca_bundle="${ca_stage}/host-ca"
        _stage_host_ca_bundle "${ca_bundle}" || : > "${ca_bundle}"

        ensure_yscope_dev_utils_submodule

        docker buildx build \
            --platform "${platform}" \
            --build-context "host-ca=${ca_stage}" \
            --tag "${tag}" \
            "${output}" \
            -f "${_REPO_ROOT}/tools/build-packages/dependency-image/Dockerfile" \
            "${_REPO_ROOT}"
    )
}
