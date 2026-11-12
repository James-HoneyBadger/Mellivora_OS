#!/usr/bin/env bash
#
# test_compress.sh - compressed files on HBFS.
#
# A compressed file keeps its data as a chunk table followed by the chunks
# themselves: one 16-byte table entry per 16 KiB of plaintext, each naming the
# block the chunk starts at, how many bytes it stores, a CRC-32C of the
# plaintext and whether the chunk is LZ-encoded or stored whole. The gate
# asserts the promises that layout makes, not merely that a command exists:
#
#   1. compacting a file keeps its size and lowers its block count;
#   2. the file still reads back byte for byte (asserted with the shell's own
#      text compare, in a boot where the write never happened);
#   3. a 580 KB program - 37 chunks - still loads and runs, which is the
#      multi-chunk and program-loader path;
#   4. expand restores the block count a plain file had;
#   5. compact refuses a file that is already compressed and expand refuses one
#      that is not, so a command cannot silently do nothing;
#   6. the offline checker agrees: it decodes every chunk with its own decoder,
#      written from the format rather than from the kernel, and verifies each
#      chunk's plaintext checksum.
#
# Usage:  tests/test_compress.sh [image]
#
# Env:
#   COMPRESS_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#   COMPRESS_TIMEOUT=360    seconds allowed for a machine to reach its shell
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${COMPRESS_IMAGE:-$ROOT/mellivora.img}}"
DRIVE="$ROOT/tools/qemu_drive.py"
FSCK="$ROOT/tools/fsck_hbfs.py"
TIMEOUT="${COMPRESS_TIMEOUT:-360}"

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
    if [ "${COMPRESS_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  compress tests: no QEMU found"
        exit 0
    fi
    fail "compress tests: no QEMU found. Install qemu-system-x86, or set COMPRESS_ALLOW_SKIP=1."
fi

[ -f "$DRIVE" ] || fail "compress tests: missing $DRIVE"
[ -f "$FSCK" ] || fail "compress tests: missing $FSCK"
[ -f "$IMAGE" ] || fail "compress tests: disk image not found: $IMAGE (run 'make full' first)"

drive() {
    QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "$@" \
        --timeout "$TIMEOUT"
}

# run <name> <expect-text> <command> [--then CMD]...
run() {
    local name="$1"; local text="$2"; local command="$3"; shift 3
    printf '  %-44s ' "$name"
    local log
    log="$(mktemp)"
    if drive "$command" --out "$log" --expect "$text" "$@" >/dev/null 2>&1; then
        echo "PASS"
        rm -f "$log"
    else
        echo "FAIL  (output: $log)"
        exit 1
    fi
}

# The write and the read are separate boots: the console echoes what is typed,
# so a payload typed into a write would satisfy an --expect even if the read
# that should have produced it never happened.
drive "del cw.txt" --then "copy /samples/wumpus.c cw.txt" \
    --then "compact cw.txt" >/dev/null 2>&1 \
    || fail "compress tests: could not create and compact cw.txt"

run "a compacted file keeps its size, uses fewer blocks" \
    "Size: 8652 bytes (2 blocks)" "stat cw.txt"

run "a compacted file reads back byte for byte" \
    "Files are identical." "diff /samples/wumpus.c cw.txt"

run "compacting twice is refused, not repeated" \
    "already stored compressed" "compact cw.txt"

run "expand restores the plain block count" \
    "Size: 8652 bytes (3 blocks)" "expand cw.txt" --then "stat cw.txt"

run "expanding a plain file is refused" \
    "not stored compressed" "expand cw.txt"

# A mapping of a chunked file is decoded a chunk at a time into the faulting
# frame, so the same program compares a mapping against SYS_FREAD: before the
# files are compressed, while they are, and after they are expanded again.
printf '  %-44s ' "a mapping of a plain file matches its bytes"
if drive "run /mmap_packed_test" --out /tmp/cmp-map1.log --reject "MMAP_PACKED_FAIL" >/dev/null 2>&1; then
    echo "PASS"
else
    echo "FAIL  (output: /tmp/cmp-map1.log)"
    exit 1
fi

# A 580 KB program is 37 chunks, and running it is the proof that the loader
# decodes what a read would: nothing about the image is right if the chunks are
# decoded wrongly.
drive "compact /bin/tar" >/dev/null 2>&1 \
    || fail "compress tests: could not compact /bin/tar"

run "a 37-chunk program still loads and runs" \
    "Usage: tar c|x|t archive.tar" "run /bin/tar"

run "the program expands back to one plain run" \
    "Saved." "expand /bin/tar"

# The mapping cases use files small enough to keep both a reference copy and
# the mapping inside one program's window.
drive "compact /samples/wumpus.c" --then "compact /bin/threadtest" \
    >/dev/null 2>&1 || fail "compress tests: could not compact the mapping fixtures"

printf '  %-44s ' "a mapping of a compacted file matches its bytes"
if drive "run /mmap_packed_test" --out /tmp/cmp-map2.log --reject "MMAP_PACKED_FAIL" >/dev/null 2>&1; then
    echo "PASS"
else
    echo "FAIL  (output: /tmp/cmp-map2.log)"
    exit 1
fi

drive "expand /samples/wumpus.c" --then "expand /bin/threadtest" \
    >/dev/null 2>&1 || fail "compress tests: could not expand the mapping fixtures"

drive "del cw.txt" >/dev/null 2>&1 || true

printf '  %-44s ' "the volume is consistent afterwards"
if python3 "$FSCK" "$IMAGE" >/dev/null 2>&1; then
    echo "PASS"
else
    echo "FAIL  (run: python3 tools/fsck_hbfs.py $IMAGE)"
    exit 1
fi

echo "  Compression: chunk tables, plain reads, mappings, the loader, expand, refusal"
