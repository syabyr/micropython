#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-/dev/tty.usbmodem3}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEVICE_SCRIPT="$SCRIPT_DIR/verify_fs_device.py"

if ! command -v mpremote >/dev/null 2>&1; then
    echo "ERROR: mpremote not found in PATH"
    echo "Install with: pip install mpremote"
    exit 1
fi

if [[ ! -f "$DEVICE_SCRIPT" ]]; then
    echo "ERROR: device script not found: $DEVICE_SCRIPT"
    exit 1
fi

echo "[INFO] Using port: $PORT"
echo "[INFO] Running device verification script..."

mpremote connect "$PORT" run "$DEVICE_SCRIPT"

echo "[INFO] Verification finished."
