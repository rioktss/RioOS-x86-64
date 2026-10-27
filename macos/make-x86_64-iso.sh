#!/usr/bin/env bash
set -euo pipefail
PLATFORM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$PLATFORM_DIR/.." && pwd)"
exec bash "$PROJECT_ROOT/tools/x86_64/make-x86_64-iso.sh" "$@"
