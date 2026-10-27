#!/usr/bin/env bash
set -euo pipefail
PLATFORM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$PLATFORM_DIR/.." && pwd)"
export RIOOS_PLATFORM_DIR="$PLATFORM_DIR"
export RIOOS_DISPLAY="${RIOOS_DISPLAY:-gtk}"
exec bash "$PROJECT_ROOT/tools/x86_64/run-ui.sh" "$@"
