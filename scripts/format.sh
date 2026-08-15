#!/usr/bin/env bash
# format.sh — run the same clang-format check CI runs, locally, before you push.
#
# Mirrors the `format` job in .github/workflows/ci.yml exactly: same file set
# (client/src, client/tests, *.cpp/*.h/*.hpp), same binary (clang-format-18),
# same flags. Also checks the installed clang-format-18 matches the version
# pinned in .devcontainer/Dockerfile, since a silent point-release drift is
# what caused the CI/format failure recorded in DEVIATIONS.md 2026-08-07 —
# a mismatch here would just reproduce that bug locally instead of catching it.
#
# Usage:
#   scripts/format.sh          # check only, exits non-zero if anything's unformatted
#   scripts/format.sh --fix    # reformat in place

set -euo pipefail

PINNED_VERSION="1:18.1.3-1ubuntu1"
BINARY="clang-format-18"

if ! command -v "$BINARY" >/dev/null 2>&1; then
    echo "error: $BINARY not found on PATH (expected inside the devcontainer)" >&2
    exit 1
fi

installed_version=$(dpkg-query -W -f='${Version}' clang-format-18 2>/dev/null || echo "unknown")
if [ "$installed_version" != "$PINNED_VERSION" ]; then
    echo "warning: clang-format-18 is $installed_version, pinned version is $PINNED_VERSION" >&2
    echo "warning: results may not match CI — rebuild the devcontainer to pick up the pin" >&2
fi

files=$(find client/src client/tests -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) 2>/dev/null || true)
if [ -z "$files" ]; then
    echo "No C++ source files found to format-check."
    exit 0
fi

if [ "${1:-}" = "--fix" ]; then
    "$BINARY" -i $files
    echo "Reformatted. Run 'scripts/format.sh' again to confirm it's clean."
else
    "$BINARY" --dry-run --Werror $files
    echo "All files clang-format-clean."
fi
