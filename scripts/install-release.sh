#!/bin/sh
set -eu

PORT=${1:-}
if [ -z "$PORT" ]; then
    echo "Usage: $0 SERIAL_PORT" >&2
    exit 2
fi

IMAGE_URL=${BEEBEM_IMAGE_URL:-https://beebem.webassembly.link/flash/beebem-esp32-s3-v1.4.bin}
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT HUP INT TERM
IMAGE="$TEMP_DIR/beebem-esp32-s3.bin"

curl --fail --location "$IMAGE_URL" --output "$IMAGE"
uvx --from esptool esptool --chip esp32s3 --port "$PORT" --baud 460800 \
    write-flash 0x0 "$IMAGE"
