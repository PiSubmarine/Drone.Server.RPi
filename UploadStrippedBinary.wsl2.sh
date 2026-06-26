#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_FILE="${SCRIPT_DIR}/out/build/pisubmarine-wsl2-debug/app/PiSubmarine.Drone.Server.RPi.App.Stripped"
REMOTE_HOST="pisubmarine@PiSubmarine"
REMOTE_PORT="22"
REMOTE_DIR="${PISUBMARINE_UPLOAD_REMOTE_DIR:-/home/pisubmarine/CLion/debug}"
REMOTE_FILE="${REMOTE_DIR}/PiSubmarine.Drone.Server.RPi.App.Stripped"
PASSWORD_FILE="${SCRIPT_DIR}/.pisubmarine-upload-password"

upload_with_password() {
    local password="$1"

    if ! command -v sshpass >/dev/null 2>&1; then
        echo "sshpass is required for password-based upload." >&2
        echo "Install it or configure SSH keys to avoid passwords entirely." >&2
        exit 1
    fi

    SSHPASS="${password}" sshpass -e ssh -p "${REMOTE_PORT}" "${REMOTE_HOST}" "mkdir -p '${REMOTE_DIR}' && df -h '${REMOTE_DIR}'"
    SSHPASS="${password}" sshpass -e scp -P "${REMOTE_PORT}" "${LOCAL_FILE}" "${REMOTE_HOST}:${REMOTE_FILE}"
}

if [[ ! -f "${LOCAL_FILE}" ]]; then
    echo "Local file not found: ${LOCAL_FILE}" >&2
    echo "Run CreateStrippedBinary.wsl2.sh first." >&2
    exit 1
fi

echo "Local file:"
ls -lh "${LOCAL_FILE}"

if ssh -o BatchMode=yes -p "${REMOTE_PORT}" "${REMOTE_HOST}" "mkdir -p '${REMOTE_DIR}' && df -h '${REMOTE_DIR}'" 2>/dev/null; then
    scp -P "${REMOTE_PORT}" "${LOCAL_FILE}" "${REMOTE_HOST}:${REMOTE_FILE}"
elif [[ -n "${PISUBMARINE_UPLOAD_PASSWORD:-}" ]]; then
    upload_with_password "${PISUBMARINE_UPLOAD_PASSWORD}"
elif [[ -f "${PASSWORD_FILE}" ]]; then
    upload_with_password "$(tr -d '\r\n' < "${PASSWORD_FILE}")"
else
    echo "Upload authentication is not configured." >&2
    echo "Preferred: set up SSH keys for ${REMOTE_HOST}." >&2
    echo "Alternative 1: export PISUBMARINE_UPLOAD_PASSWORD in WSL2." >&2
    echo "Alternative 2: store the password in ${PASSWORD_FILE}." >&2
    exit 1
fi

echo "Uploaded:"
echo "  ${LOCAL_FILE}"
echo "to:"
echo "  ${REMOTE_HOST}:${REMOTE_FILE}"
