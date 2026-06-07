#!/usr/bin/env bash
#
# Build cadd0040 inside a Rocky Linux 8 Docker image and copy the release
# binary to dist/rocky8/cadd0040 on the host.
#
# Default target is linux/amd64 (x86_64) for contest / RHEL-compatible servers.
#
# Usage:
#   ./scripts/docker-build-rocky8.sh
#   PLATFORM=linux/arm64 ./scripts/docker-build-rocky8.sh   # override for native ARM
#   IMAGE=cadd0040-rocky8 OUT_DIR=./dist/rocky8 ./scripts/docker-build-rocky8.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${IMAGE:-cadd0040-rocky8}"
OUT_DIR="${OUT_DIR:-${ROOT}/dist/rocky8}"
BINARY="${OUT_DIR}/cadd0040"
DOCKERFILE="${ROOT}/docker/rocky8/Dockerfile"
PLATFORM="${PLATFORM:-linux/amd64}"

if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker is not installed or not on PATH" >&2
    exit 1
fi

echo "==> Target platform: ${PLATFORM}"
echo "==> Building Docker image: ${IMAGE}"
docker build --platform "${PLATFORM}" -f "${DOCKERFILE}" -t "${IMAGE}" "${ROOT}"

mkdir -p "${OUT_DIR}"

echo "==> Extracting release binary to ${BINARY}"
cid="$(docker create --platform "${PLATFORM}" "${IMAGE}")"
trap 'docker rm -f "${cid}" >/dev/null 2>&1 || true' EXIT
docker cp "${cid}:/opt/cadd0040/cadd0040" "${BINARY}"
docker rm "${cid}" >/dev/null
trap - EXIT

chmod +x "${BINARY}"

echo "==> Done"
echo "Binary: ${BINARY}"
file "${BINARY}"
