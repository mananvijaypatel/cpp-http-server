#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
find src include tests \( -name '*.cpp' -o -name '*.hpp' \) -exec clang-format -i {} +
echo "Formatted."
