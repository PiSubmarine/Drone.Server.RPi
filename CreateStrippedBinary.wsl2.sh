#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/out/build/pisubmarine-wsl2-debug/app"
INPUT_FILE="${BUILD_DIR}/PiSubmarine.Drone.Server.RPi.App"
STRIPPED_FILE="${BUILD_DIR}/PiSubmarine.Drone.Server.RPi.App.Stripped"
DEBUG_FILE="${BUILD_DIR}/PiSubmarine.Drone.Server.RPi.App.debug"

find_tool() {
    local preferred="$1"
    local fallback="$2"

    if command -v "${preferred}" >/dev/null 2>&1; then
        command -v "${preferred}"
        return 0
    fi

    if command -v "${fallback}" >/dev/null 2>&1; then
        command -v "${fallback}"
        return 0
    fi

    echo "Required tool not found: ${preferred} or ${fallback}" >&2
    return 1
}

OBJCOPY="$(find_tool aarch64-linux-gnu-objcopy objcopy)"
STRIP="$(find_tool aarch64-linux-gnu-strip strip)"

if [[ ! -f "${INPUT_FILE}" ]]; then
    echo "Input file not found: ${INPUT_FILE}" >&2
    exit 1
fi

cp "${INPUT_FILE}" "${STRIPPED_FILE}"
"${OBJCOPY}" --only-keep-debug "${INPUT_FILE}" "${DEBUG_FILE}"
"${STRIP}" --strip-debug --strip-unneeded "${STRIPPED_FILE}"
"${OBJCOPY}" --add-gnu-debuglink="${DEBUG_FILE}" "${STRIPPED_FILE}"

echo "Created:"
echo "  ${STRIPPED_FILE}"
echo "  ${DEBUG_FILE}"
