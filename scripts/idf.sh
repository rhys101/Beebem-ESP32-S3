#!/bin/sh
set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IDF_PATH=${IDF_PATH:-"$PROJECT_ROOT/.deps/esp-idf"}
IDF_TOOLS_PATH=${IDF_TOOLS_PATH:-"$PROJECT_ROOT/.deps/espressif"}
IDF_PYTHON_ENV_PATH=${IDF_PYTHON_ENV_PATH:-"$PROJECT_ROOT/.venv"}

export IDF_PATH IDF_TOOLS_PATH IDF_PYTHON_ENV_PATH

if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "ESP-IDF was not found at $IDF_PATH" >&2
    echo "Run scripts/setup.sh, or set IDF_PATH to an ESP-IDF 5.5 checkout." >&2
    exit 2
fi

# export.sh supplies the exact compiler and utility paths installed for this IDF.
# Its normal status banner is hidden so build and flash errors stay prominent.
. "$IDF_PATH/export.sh" >/dev/null
exec python "$IDF_PATH/tools/idf.py" "$@"
