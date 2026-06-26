#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_FILE="${SCRIPT_DIR}/out/build/pisubmarine-wsl2-debug/app/PiSubmarine.Drone.Server.RPi.App.Stripped"
REMOTE_HOST="pisubmarine@PiSubmarine"
REMOTE_PORT="22"
REMOTE_DIR="${PISUBMARINE_UPLOAD_REMOTE_DIR:-/home/pisubmarine/CLion/debug}"
REMOTE_FILE="${REMOTE_DIR}/PiSubmarine.Drone.Server.RPi.App.Stripped"
PASSWORD_FILE="${SCRIPT_DIR}/.pisubmarine-upload-password"

get_local_mtime() {
    stat -c %Y "${LOCAL_FILE}"
}

get_remote_mtime_command() {
    cat <<EOF
if [[ -f '${REMOTE_FILE}' ]]; then
    stat -c %Y '${REMOTE_FILE}'
fi
EOF
}

get_set_remote_mtime_command() {
    local local_mtime="$1"

    cat <<EOF
touch -d "@${local_mtime}" '${REMOTE_FILE}'
EOF
}

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

remote_exec() {
    ssh -o BatchMode=yes -p "${REMOTE_PORT}" "${REMOTE_HOST}" "$1"
}

remote_exec_with_password() {
    local password="$1"
    SSHPASS="${password}" sshpass -e ssh -p "${REMOTE_PORT}" "${REMOTE_HOST}" "$2"
}

upload_if_needed_with_ssh() {
    local local_mtime remote_mtime

    local_mtime="$(get_local_mtime)"

    remote_exec "mkdir -p '${REMOTE_DIR}' && df -h '${REMOTE_DIR}'" >/dev/null
    remote_mtime="$(remote_exec "$(get_remote_mtime_command)" 2>/dev/null || true)"

    if [[ -n "${remote_mtime}" && "${remote_mtime}" == "${local_mtime}" ]]; then
        echo "Remote file is up to date."
        echo "  ${REMOTE_HOST}:${REMOTE_FILE}"
        return 0
    fi

    scp -P "${REMOTE_PORT}" "${LOCAL_FILE}" "${REMOTE_HOST}:${REMOTE_FILE}"
    remote_exec "$(get_set_remote_mtime_command "${local_mtime}")" >/dev/null
}

upload_if_needed_with_password() {
    local password="$1"
    local local_mtime remote_mtime

    local_mtime="$(get_local_mtime)"

    remote_exec_with_password "${password}" "mkdir -p '${REMOTE_DIR}' && df -h '${REMOTE_DIR}'" >/dev/null
    remote_mtime="$(remote_exec_with_password "${password}" "$(get_remote_mtime_command)" 2>/dev/null || true)"

    if [[ -n "${remote_mtime}" && "${remote_mtime}" == "${local_mtime}" ]]; then
        echo "Remote file is up to date."
        echo "  ${REMOTE_HOST}:${REMOTE_FILE}"
        return 0
    fi

    upload_with_password "${password}"
    remote_exec_with_password "${password}" "$(get_set_remote_mtime_command "${local_mtime}")" >/dev/null
}

if [[ ! -f "${LOCAL_FILE}" ]]; then
    echo "Local file not found: ${LOCAL_FILE}" >&2
    echo "Run CreateStrippedBinary.wsl2.sh first." >&2
    exit 1
fi

echo "Local file:"
ls -lh "${LOCAL_FILE}"

if ssh -o BatchMode=yes -p "${REMOTE_PORT}" "${REMOTE_HOST}" "true" 2>/dev/null; then
    upload_if_needed_with_ssh
elif [[ -n "${PISUBMARINE_UPLOAD_PASSWORD:-}" ]]; then
    upload_if_needed_with_password "${PISUBMARINE_UPLOAD_PASSWORD}"
elif [[ -f "${PASSWORD_FILE}" ]]; then
    upload_if_needed_with_password "$(tr -d '\r\n' < "${PASSWORD_FILE}")"
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
