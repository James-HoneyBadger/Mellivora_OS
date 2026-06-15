#!/usr/bin/env bash
# Lightweight NASM source hygiene checks.
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <file> [file ...]"
    exit 2
fi

status=0

for file in "$@"; do
    if [[ ! -f "$file" ]]; then
        echo "[nasm-lint] FAIL missing file: $file"
        status=1
        continue
    fi

    if LC_ALL=C grep -q $'\r' "$file"; then
        echo "[nasm-lint] FAIL CRLF line endings: $file"
        status=1
    fi

    if ! python3 - "$file" <<'PY'
import pathlib
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
sys.exit(1 if b"\x00" in data else 0)
PY
    then
        echo "[nasm-lint] FAIL NUL byte detected: $file"
        status=1
    fi

    # Require a final newline for cleaner diffs and parser stability.
    if [[ -n "$(tail -c 1 "$file" 2>/dev/null || true)" ]]; then
        echo "[nasm-lint] FAIL missing trailing newline: $file"
        status=1
    fi

    # Warn on TABs in comments; this is a warning-only signal today.
    if grep -n $'\t.*;' "$file" >/dev/null 2>&1; then
        echo "[nasm-lint] WARN tab before comment in $file"
    fi

    if [[ $status -eq 0 ]]; then
        echo "[nasm-lint] PASS $file"
    fi
done

exit $status
