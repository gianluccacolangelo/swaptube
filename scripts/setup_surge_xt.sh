#!/bin/bash

set -euo pipefail

check_command_available() {
    command -v "$1" > /dev/null 2>&1 || {
        echo "setup_surge_xt.sh: required command '$1' was not found."
        exit 1
    }
}

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
PATCH_FILE="${REPO_ROOT}/patches/surge-xt-embed.patch"

TARGET_DIR=${1:-"${REPO_ROOT}/.tmp/vendor/surge-xt"}
SURGE_REMOTE_URL=${SWAPTUBE_SURGE_XT_URL:-"https://github.com/surge-synthesizer/surge.git"}
SURGE_REF=${SWAPTUBE_SURGE_XT_REF:-"c6195b2"}

check_command_available "git"

mkdir -p "$(dirname "${TARGET_DIR}")"

if [ ! -d "${TARGET_DIR}/.git" ]; then
    echo "setup_surge_xt.sh: cloning Surge XT into ${TARGET_DIR}"
    git clone "${SURGE_REMOTE_URL}" "${TARGET_DIR}"
fi

if [ ! -f "${TARGET_DIR}/CMakeLists.txt" ]; then
    echo "setup_surge_xt.sh: ${TARGET_DIR} is not a Surge XT checkout."
    exit 1
fi

if git -C "${TARGET_DIR}" diff --quiet && git -C "${TARGET_DIR}" diff --cached --quiet; then
    CURRENT_HEAD=$(git -C "${TARGET_DIR}" rev-parse --short HEAD)
    if [ "${CURRENT_HEAD}" != "${SURGE_REF}" ]; then
        echo "setup_surge_xt.sh: checking out Surge XT ref ${SURGE_REF}"
        git -C "${TARGET_DIR}" fetch --tags origin || true
        git -C "${TARGET_DIR}" checkout "${SURGE_REF}"
    fi
else
    echo "setup_surge_xt.sh: existing checkout has local changes; leaving current ref in place."
fi

echo "setup_surge_xt.sh: syncing submodules"
git -C "${TARGET_DIR}" submodule update --init --recursive

if git -C "${TARGET_DIR}" apply --reverse --check "${PATCH_FILE}" > /dev/null 2>&1; then
    echo "setup_surge_xt.sh: Swaptube embed patch already applied."
elif git -C "${TARGET_DIR}" apply --check "${PATCH_FILE}" > /dev/null 2>&1; then
    echo "setup_surge_xt.sh: applying Swaptube embed patch."
    git -C "${TARGET_DIR}" apply "${PATCH_FILE}"
else
    echo "setup_surge_xt.sh: embed patch does not apply cleanly."
    echo "setup_surge_xt.sh: the upstream Surge XT checkout likely changed and needs a patch refresh."
    exit 1
fi

echo "setup_surge_xt.sh: Surge XT is ready at ${TARGET_DIR}"
