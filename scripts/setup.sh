#!/bin/sh
set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IDF_PATH=${IDF_PATH:-"$PROJECT_ROOT/.deps/esp-idf"}
IDF_TOOLS_PATH=${IDF_TOOLS_PATH:-"$PROJECT_ROOT/.deps/espressif"}
IDF_VERSION=${IDF_VERSION:-v5.5.5}
IDF_CONSTRAINTS_VERSION=${IDF_CONSTRAINTS_VERSION:-v5.5}

command -v git >/dev/null 2>&1 || { echo "git is required" >&2; exit 2; }
command -v uv >/dev/null 2>&1 || { echo "uv is required: https://docs.astral.sh/uv/" >&2; exit 2; }

mkdir -p "$PROJECT_ROOT/.deps"
if [ ! -d "$IDF_PATH/.git" ]; then
    git clone --recursive --branch "$IDF_VERSION" --depth 1 \
        https://github.com/espressif/esp-idf.git "$IDF_PATH"
fi

uv venv --python 3.12 "$PROJECT_ROOT/.venv"
CONSTRAINTS="$IDF_TOOLS_PATH/espidf.constraints.$IDF_CONSTRAINTS_VERSION.txt"
mkdir -p "$IDF_TOOLS_PATH"
if [ ! -f "$CONSTRAINTS" ]; then
    curl --fail --location \
        "https://dl.espressif.com/dl/esp-idf/espidf.constraints.$IDF_CONSTRAINTS_VERSION.txt" \
        --output "$CONSTRAINTS"
fi
uv pip install --python "$PROJECT_ROOT/.venv/bin/python" \
    --constraint "$CONSTRAINTS" \
    -r "$IDF_PATH/tools/requirements/requirements.core.txt"
uv pip install --python "$PROJECT_ROOT/.venv/bin/python" \
    "Pillow>=11,<13" "pyserial>=3.5,<4"

export IDF_PATH IDF_TOOLS_PATH
"$PROJECT_ROOT/.venv/bin/python" "$IDF_PATH/tools/idf_tools.py" \
    --non-interactive install --targets=esp32s3
echo "ESP-IDF $IDF_VERSION is ready. Next:"
echo "  .venv/bin/python tools/fetch_assets.py"
echo "  scripts/idf.sh build"
