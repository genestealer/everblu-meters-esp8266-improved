#!/usr/bin/env bash
# Format (or check) the everblu_meter ESPHome component sources with ESPHome's clang-format style.
#
# Runs clang-format over the component's own C++ sources using the .clang-format in
# ESPHOME/components/everblu_meter/ (a verbatim copy of ESPHome's upstream style:
# https://github.com/esphome/esphome/blob/dev/.clang-format).
#
# Only the component's own files (everblu_meter.cpp / .h) are formatted. The portable
# library under src/ keeps its own house style and is not touched.
#
# If clang-format is not on PATH, the latest pip package is installed automatically. Both
# this script and CI use the latest clang-format (no version to maintain); if a new release
# reformats the sources and CI fails, re-run with --fix and commit the result.
#
# Usage:
#   ESPHOME/format-component.sh          # check only (non-zero exit if changes needed)
#   ESPHOME/format-component.sh --fix    # reformat in place
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPONENT_DIR="${SCRIPT_DIR}/components/everblu_meter"

FIX=0
if [ "${1:-}" = "--fix" ]; then
  FIX=1
elif [ -n "${1:-}" ]; then
  echo "Unknown argument: $1 (expected --fix or nothing)" >&2
  exit 2
fi

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format not found; installing the latest clang-format via pip..."
  python -m pip install --quiet clang-format
fi

mapfile -t FILES < <(find "${COMPONENT_DIR}" -type f \( -name '*.cpp' -o -name '*.h' \) | sort)
if [ ${#FILES[@]} -eq 0 ]; then
  echo "No C++ sources found in ${COMPONENT_DIR}" >&2
  exit 1
fi

if [ "${FIX}" -eq 1 ]; then
  clang-format -style=file -i "${FILES[@]}"
  echo "Formatted ${#FILES[@]} file(s)."
else
  if ! clang-format -style=file --dry-run -Werror "${FILES[@]}"; then
    echo "Formatting issues found. Fix with: ESPHOME/format-component.sh --fix"
    exit 1
  fi
  echo "All component sources are correctly formatted."
fi
