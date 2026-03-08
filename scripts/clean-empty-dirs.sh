#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/clean-empty-dirs.sh [--check] [DIR...]

  --check   Only report empty directories and exit non-zero if any found.
            Without this flag, empty directories are removed.

Default DIR list: src/server tests
EOF
}

check_only=0
if [[ "${1:-}" == "--check" ]]; then
  check_only=1
  shift
fi

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ "$#" -gt 0 ]]; then
  roots=("$@")
else
  roots=("src/server" "tests")
fi

scan_empty_dirs() {
  find "$@" -type d -empty | sort
}

mapfile -t empty_dirs < <(scan_empty_dirs "${roots[@]}")

if [[ ${#empty_dirs[@]} -eq 0 ]]; then
  echo "No empty directories found."
  exit 0
fi

printf 'Empty directories (%d):\n' "${#empty_dirs[@]}"
printf '  %s\n' "${empty_dirs[@]}"

if [[ "$check_only" -eq 1 ]]; then
  exit 1
fi

# Remove from deepest path first.
while IFS= read -r dir; do
  rmdir "$dir" || true
done < <(printf '%s\n' "${empty_dirs[@]}" | awk '{ print length($0), $0 }' | sort -rn | cut -d' ' -f2-)

mapfile -t remain < <(scan_empty_dirs "${roots[@]}")
if [[ ${#remain[@]} -gt 0 ]]; then
  printf 'Remaining empty directories after cleanup (%d):\n' "${#remain[@]}"
  printf '  %s\n' "${remain[@]}"
  exit 1
fi

echo "Removed ${#empty_dirs[@]} empty directories."
