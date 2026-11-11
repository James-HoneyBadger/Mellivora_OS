#!/usr/bin/env bash
#
# test_streams.sh - alternate data streams on HBFS.
#
# A stream is a second content of a file, reached as FILE:STREAM. It is stored
# as a directory entry of type FTYPE_STREAM named "FILE:STREAM", with its own
# size, blocks, permissions and xattrs. The gate asserts the properties that
# representation promises, not just that a name can be created:
#
#   1. a stream is written and listed with its size;
#   2. several streams of one file are independent;
#   3. rewriting a stream replaces its content;
#   4. dir and readdir hide streams, because they are part of the file;
#   5. deleting the file deletes its streams;
#   6. renaming the file moves its streams;
#   7. a stream cannot be created on a file that does not exist;
#   8. the volume is consistent afterwards, as the offline checker sees it.
#
# Reads are asserted in a second boot from the write, because the console
# echoes the typed command: a string typed into a write would satisfy an
# --expect even if the read never happened.
#
# Usage:  tests/test_streams.sh [image]
#
# Env:
#   STREAM_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#   STREAM_TIMEOUT=180    seconds allowed for a machine to reach its shell
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${STREAM_IMAGE:-$ROOT/mellivora.img}}"
DRIVE="$ROOT/tools/qemu_drive.py"
FSCK="$ROOT/tools/fsck_hbfs.py"
TIMEOUT="${STREAM_TIMEOUT:-180}"

fail() {
    printf '  FAIL  %s\n' "$*" >&2
    exit 1
}

find_qemu() {
    if [ -n "${QEMU:-}" ]; then
        printf '%s' "$QEMU"
        return 0
    fi
    local c
    for c in qemu-system-x86_64 qemu-system-i386; do
        if command -v "$c" >/dev/null 2>&1; then
            command -v "$c"
            return 0
        fi
    done
    if [ -x "$HOME/.local/bin/qemu-system-x86_64" ]; then
        printf '%s' "$HOME/.local/bin/qemu-system-x86_64"
        return 0
    fi
    return 1
}

if ! find_qemu >/dev/null; then
    if [ "${STREAM_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  stream tests: no QEMU found"
        exit 0
    fi
    fail "stream tests: no QEMU found. Install qemu-system-x86, or set STREAM_ALLOW_SKIP=1."
fi

[ -f "$DRIVE" ] || fail "stream tests: missing $DRIVE"
[ -f "$FSCK" ] || fail "stream tests: missing $FSCK"
[ -f "$IMAGE" ] || fail "stream tests: disk image not found: $IMAGE (run 'make full' first)"

# run <name> <expect-text> <command> [--then CMD]...
run() {
    local name="$1"; local text="$2"; local command="$3"; shift 3
    printf '  %-42s ' "$name"
    local log
    log="$(mktemp)"
    if QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "$command" \
            --timeout "$TIMEOUT" --out "$log" --expect "$text" "$@" >/dev/null 2>&1; then
        echo "PASS"
        rm -f "$log"
    else
        echo "FAIL  (output: $log)"
        exit 1
    fi
}

# run_reject <name> <rejected-text> <command> [--then CMD]...
run_reject() {
    local name="$1"; local text="$2"; local command="$3"; shift 3
    printf '  %-42s ' "$name"
    local log
    log="$(mktemp)"
    if QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "$command" \
            --timeout "$TIMEOUT" --out "$log" --reject "$text" "$@" >/dev/null 2>&1; then
        echo "PASS"
        rm -f "$log"
    else
        echo "FAIL  (output: $log)"
        exit 1
    fi
}

run "a stream is written and listed" "draft  11 bytes" "touch st1.txt" \
    --then "stream write st1.txt draft first-draft" \
    --then "stream list st1.txt" \
    --then "del st1.txt"

# Two streams of one file, written here and read in a fresh boot.
QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "del st2.txt" \
    --then "touch st2.txt" \
    --then "stream write st2.txt alpha stream-payload-alpha" \
    --then "stream write st2.txt beta stream-payload-beta" \
    --timeout "$TIMEOUT" >/dev/null 2>&1 || fail "stream tests: could not create st2.txt's streams"

run "a second stream is independent" "stream-payload-beta" "cat st2.txt:beta" \
    --then "cat st2.txt:alpha" \
    --then "del st2.txt"

run "rewriting a stream replaces it" "draft  18 bytes" "touch st3.txt" \
    --then "stream write st3.txt draft short" \
    --then "stream write st3.txt draft a-longer-draft-now" \
    --then "stream list st3.txt" \
    --then "del st3.txt"

# Streams are hidden from a listing. The create is its own boot so the
# transcript of the dir boot has no "file:stream" in it at all.
QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "del st4.txt" \
    --then "touch st4.txt" \
    --then "stream write st4.txt hidden hidden-payload" \
    --timeout "$TIMEOUT" >/dev/null 2>&1 || fail "stream tests: could not create st4.txt's stream"

run_reject "streams are hidden from dir" "st4.txt:hidden" "dir" \
    --then "del st4.txt"

run "deleting the file deletes its streams" "no streams on st5.txt" "touch st5.txt" \
    --then "stream write st5.txt gone payload" \
    --then "del st5.txt" \
    --then "touch st5.txt" \
    --then "stream list st5.txt" \
    --then "del st5.txt"

# Rename in one boot, read in the next: the old base is gone and the stream
# has to be reachable under the new one.
QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "del st6.txt" \
    --then "del st6-renamed.txt" \
    --then "touch st6.txt" \
    --then "stream write st6.txt draft stream-payload-gamma" \
    --then "ren st6.txt st6-renamed.txt" \
    --timeout "$TIMEOUT" >/dev/null 2>&1 || fail "stream tests: could not rename st6.txt"

run "renaming the file moves its streams" "stream-payload-gamma" \
    "cat st6-renamed.txt:draft" --then "del st6-renamed.txt"

run "a stream needs its file" "no such file" \
    "stream write st7-missing.txt draft payload"

printf '  %-42s ' "the volume is consistent afterwards"
if python3 "$FSCK" "$IMAGE" >/dev/null 2>&1; then
    echo "PASS"
else
    echo "FAIL  (run: python3 tools/fsck_hbfs.py $IMAGE)"
    exit 1
fi

echo "  Streams: written, listed, read, hidden, deleted and renamed with their file"
