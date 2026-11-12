#!/usr/bin/env bash
#
# test_acl.sh - inherited access control entries.
#
# An ACE whose value carries the `i` flag is passed down: a file created in
# such a directory gets the ACE with the flag cleared (it has no children), and
# a subdirectory gets it with the flag kept, so the grant walks a whole tree
# instead of stopping one level below the directory that set it. The gate
# asserts what makes that useful rather than that a command ran:
#
#   1. a file created in an inheriting directory carries the ACE, without the
#      flag;
#   2. a subdirectory carries it with the flag, so a tree keeps inheriting;
#   3. a third level confirms the walk continues rather than stopping at one;
#   4. an ACE without the flag is not inherited at all - inheritance is
#      something a directory is told to do, not a side effect of having an ACL;
#   5. the parent's own ACE is unchanged by any of it;
#   6. the volume is consistent afterwards, as the offline checker sees it.
#
# Usage:  tests/test_acl.sh [image]
#
# Env:
#   ACL_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#   ACL_TIMEOUT=240    seconds allowed for a machine to reach its shell
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${ACL_IMAGE:-$ROOT/mellivora.img}}"
DRIVE="$ROOT/tools/qemu_drive.py"
FSCK="$ROOT/tools/fsck_hbfs.py"
TIMEOUT="${ACL_TIMEOUT:-240}"

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
    if [ "${ACL_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  acl tests: no QEMU found"
        exit 0
    fi
    fail "acl tests: no QEMU found. Install qemu-system-x86, or set ACL_ALLOW_SKIP=1."
fi

[ -f "$DRIVE" ] || fail "acl tests: missing $DRIVE"
[ -f "$FSCK" ] || fail "acl tests: missing $FSCK"
[ -f "$IMAGE" ] || fail "acl tests: disk image not found: $IMAGE (run 'make full' first)"

drive() {
    QEMU="$(find_qemu)" timeout "$TIMEOUT" python3 "$DRIVE" "$IMAGE" "$@" \
        --timeout "$TIMEOUT"
}

run() {
    local name="$1"; local text="$2"; local command="$3"; shift 3
    printf '  %-46s ' "$name"
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

run_reject() {
    local name="$1"; local text="$2"; local command="$3"; shift 3
    printf '  %-46s ' "$name"
    local log
    log="$(mktemp)"
    if drive "$command" --out "$log" --reject "$text" "$@" >/dev/null 2>&1; then
        echo "PASS"
        rm -f "$log"
    else
        echo "FAIL  (output: $log)"
        exit 1
    fi
}

# A directory that hands its grant down, with a file and two levels of
# directory in it.
drive "del iad" --then "mkdir iad" --then "acl set iad u5 7i" \
    --then "cd iad" --then "touch above.txt" --then "mkdir lvl" \
    --then "cd lvl" --then "touch deep.txt" \
    >/dev/null 2>&1 || fail "acl tests: could not build the inheriting tree"

# A directory with an ACL that was never told to inherit.
drive "del nad" --then "mkdir nad" --then "acl set nad u6 6" \
    --then "cd nad" --then "touch plain.txt" \
    >/dev/null 2>&1 || fail "acl tests: could not build the plain directory"

# `acl show` reads a name in the current directory, so each case enters the
# directory it asks about rather than relying on a path it does not resolve.
run "a file inherits the ACE, without the flag" \
    "acl:u5=7" "cd iad" --then "acl show above.txt" --then "acl show lvl"

run "a subdirectory inherits it with the flag" \
    "acl:u5=7i" "cd iad" --then "acl show lvl"

run "the walk continues to the third level" \
    "acl:u5=7" "cd iad" --then "cd lvl" --then "acl show deep.txt"

run "the directory keeps the grant it was given" \
    "acl:u5=7i" "acl show iad"

run "a plain directory's child has no ACL at all" \
    "No ACL" "cd nad" --then "acl show plain.txt"

run_reject "an ACE without the flag is not inherited" \
    "acl:u6" "cd nad" --then "acl show plain.txt"

# Empty the trees before removing them: `del` refuses a directory that still
# has entries, and a leftover tree would leave ACL'd entries behind for the
# checker to look at.
drive "cd iad" --then "del above.txt" --then "cd lvl" --then "del deep.txt" \
    >/dev/null 2>&1 || true
drive "cd iad" --then "del lvl" >/dev/null 2>&1 || true
drive "del iad" --then "cd nad" --then "del plain.txt" >/dev/null 2>&1 || true
drive "del nad" >/dev/null 2>&1 || true

printf '  %-46s ' "the volume is consistent afterwards"
if python3 "$FSCK" "$IMAGE" >/dev/null 2>&1; then
    echo "PASS"
else
    echo "FAIL  (run: python3 tools/fsck_hbfs.py $IMAGE)"
    exit 1
fi

echo "  ACLs: inherited down a tree, kept by directories, refused by plain ACEs"
