#!/bin/sh
set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PORT=${1:-}
if [ -z "$PORT" ]; then
    echo "Usage: $0 SERIAL_PORT" >&2
    exit 2
fi

"$PROJECT_ROOT/scripts/setup.sh"
"$PROJECT_ROOT/.venv/bin/python" "$PROJECT_ROOT/tools/fetch_assets.py"
"$PROJECT_ROOT/scripts/build-release.sh"
"$PROJECT_ROOT/scripts/flash-release.sh" "$PORT"
