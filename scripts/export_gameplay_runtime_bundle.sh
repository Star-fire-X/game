#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

python3 "${REPO_ROOT}/tools/config_pipeline/export.py" \
  --source-dir "${REPO_ROOT}/config/excel" \
  --out-dir "${REPO_ROOT}/config/runtime" \
  --generated-at "$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
