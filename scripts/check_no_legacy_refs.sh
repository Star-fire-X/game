#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

# Only enforce on active server sources to avoid false positives in docs/comments.
SCAN_DIRS=(
  "src/server"
)

PATTERN='^[[:space:]]*#include[[:space:]]*[<"][^">]*legacy/[^">]*[">]'

matches="$(rg -n --glob '*.[ch]' --glob '*.cc' --glob '*.cpp' --glob '*.hpp' "${PATTERN}" "${SCAN_DIRS[@]}" || true)"

if [[ -n "${matches}" ]]; then
  echo "error: detected forbidden legacy include references:" >&2
  echo "${matches}" >&2
  exit 1
fi

echo "ok: no legacy include references detected in src/server"
