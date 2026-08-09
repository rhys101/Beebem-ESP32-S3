#!/bin/sh
set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=${BEEBEM_VERSION:-v1.4}
RELEASE_IMAGE="$PROJECT_ROOT/dist/beebem-esp32-s3-$VERSION.bin"
mkdir -p "$PROJECT_ROOT/dist"
"$PROJECT_ROOT/scripts/idf.sh" build

APP_BIN="$PROJECT_ROOT/build/Beebem_ESP32_S3.bin"
if [ ! -f "$APP_BIN" ]; then
    # ESP-IDF may preserve the historical target name in an existing build dir.
    APP_BIN=$(find "$PROJECT_ROOT/build" -maxdepth 1 -name '*.bin' ! -name 'bootloader.bin' | head -n 1)
fi

"$PROJECT_ROOT/.venv/bin/python" -m esptool --chip esp32s3 merge_bin \
    --flash_mode dio --flash_freq 80m --flash_size 16MB \
    --output "$RELEASE_IMAGE" \
    0x0 "$PROJECT_ROOT/build/bootloader/bootloader.bin" \
    0x8000 "$PROJECT_ROOT/build/partition_table/partition-table.bin" \
    0x10000 "$APP_BIN"

(cd "$PROJECT_ROOT/dist" && shasum -a 256 "$(basename "$RELEASE_IMAGE")") \
    > "$RELEASE_IMAGE.sha256"
echo "Release image: $RELEASE_IMAGE"
