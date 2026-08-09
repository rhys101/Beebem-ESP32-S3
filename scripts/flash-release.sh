#!/bin/sh
set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=${BEEBEM_VERSION:-v1}
IMAGE=${2:-"$PROJECT_ROOT/dist/beebem-esp32-s3-$VERSION.bin"}
PORT=${1:-}

if [ -z "$PORT" ] || [ ! -f "$IMAGE" ]; then
    echo "Usage: $0 SERIAL_PORT [FULL_IMAGE]" >&2
    exit 2
fi

"$PROJECT_ROOT/.venv/bin/python" -m esptool --chip esp32s3 \
    --port "$PORT" --baud 460800 write-flash 0x0 "$IMAGE"
