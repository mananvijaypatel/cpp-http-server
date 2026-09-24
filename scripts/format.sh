#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Pinned to match CI. Version differences produce different formatting.
FORMAT=${CLANG_FORMAT:-clang-format-19}

if ! command -v "$FORMAT" >/dev/null; then
  echo "error: $FORMAT not found. Install it or set CLANG_FORMAT." >&2
  exit 1
fi

find src include tests \( -name '*.cpp' -o -name '*.hpp' \) -exec "$FORMAT" -i {} +
echo "Formatted with $($FORMAT --version)"
