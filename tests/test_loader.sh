#!/usr/bin/env bash
#
# test_loader.sh - the HBX loader's contract (docs/spec/EXEC.md).
#
# Every row of the section 8 refusal table is a case here, plus the two images
# that must load: one at its preferred base and one that has to be relocated.
# A refusal has to name its reason and leave nothing behind, so each one is
# followed by a program that must still run.
#
# The flat path is checked too: adding a third format is not allowed to break
# the 264 programs that have no header at all.
#
# Usage:  tests/test_loader.sh [image]
#
# Env:
#   LOADER_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#   LOADER_TIMEOUT=90     per-case timeout in seconds (default 90)
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${LOADER_IMAGE:-$ROOT/mellivora.img}}"
CASE_TIMEOUT="${LOADER_TIMEOUT:-90}"
DRIVE="$ROOT/tools/qemu_drive.py"

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
    if [ -x /usr/bin/flatpak-spawn ]; then
        printf '%s' '/usr/bin/flatpak-spawn --host qemu-system-x86_64'
        return 0
    fi
    return 1
}

if ! QEMU_BIN="$(find_qemu)"; then
    if [ "${LOADER_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  loader tests: no QEMU found on this host"
        exit 0
    fi
    fail "loader tests: no QEMU found. Install qemu-system-x86, or set LOADER_ALLOW_SKIP=1."
fi
export QEMU="$QEMU_BIN"

[ -f "$DRIVE" ] || fail "loader tests: missing $DRIVE"
[ -f "$IMAGE" ] || fail "loader tests: disk image not found: $IMAGE (run 'make full' first)"

failures=0

# run_case NAME CMD EXPECT [THEN] [REJECT]
# EXPECT is the text the refusal or the program has to produce; REJECT defaults
# to a fault, because a loader that faults is not a loader that refused.
run_case() {
    local name="$1" cmd="$2" expect="$3" then="${4:-}" reject="${5:-CPU EXCEPTION|PAGE FAULT}"
    local args=("$IMAGE" "$cmd" --reject "$reject")
    [ -n "$expect" ] && args+=(--expect "$expect")
    [ -n "$then" ] && args+=(--then "$then")
    printf '  %-42s ' "$name"
    local log
    log="$(mktemp)"
    if timeout "$CASE_TIMEOUT" python3 "$DRIVE" "${args[@]}" --out "$log" >/dev/null 2>&1; then
        echo "PASS"
        rm -f "$log"
    else
        echo "FAIL  (output: $log)"
        failures=$((failures + 1))
    fi
}

# run_varying_case NAME CMD PATTERN RUNS
# Launches CMD RUNS times in one boot and requires the text matched by PATTERN
# to take at least two different values: one launch cannot show a randomised
# address.
run_varying_case() {
    local name="$1" cmd="$2" pattern="$3" runs="${4:-4}"
    local out i
    out="$(mktemp)"
    local args=("$IMAGE" "$cmd" --reject "CPU EXCEPTION|PAGE FAULT")
    for ((i = 1; i < runs; i++)); do
        args+=(--then "$cmd")
    done
    args+=(--out "$out" --wait 20)
    printf '  %-42s ' "$name"
    if ! timeout $((CASE_TIMEOUT * 2)) python3 "$DRIVE" "${args[@]}" >/dev/null 2>&1; then
        echo "FAIL"
        failures=$((failures + 1))
    else
        local -a seen
        mapfile -t seen < <(grep -o "$pattern" "$out" | sort -u)
        if [ "${#seen[@]}" -ge 2 ]; then
            echo "PASS"
        else
            echo "FAIL  (every launch used the same address)"
            failures=$((failures + 1))
        fi
    fi
    rm -f "$out"
}

echo "  loader tests: $(basename "$IMAGE"), ${CASE_TIMEOUT}s per case"

# The images that must load.
run_case "a fixed-base HBX image runs"        "hbx_ok"     "HBX_OK"
run_case "a relocated image runs"             "hbx_reloc"  "HBX_RELOC_OK"
run_case "a PE32 image runs"                  "pe_ok"      "PE_OK"

# The refusals, each followed by a program that has to still run: a half-built
# process would have taken the shell's next command with it.
run_case "a bad checksum is refused"          "hbx_badsum" "hbx: bad checksum: stored 0x" "hbx_ok"
run_case "a writable-executable section"      "hbx_wx"     "hbx: section .rwx is writable and executable" "hbx_ok"
run_case "a 64-bit image is refused"          "hbx_64"     "hbx: this system is 32-bit" "hbx_ok"
run_case "an image that cannot be moved"      "hbx_noreloc" "hbx: cannot load at 0x00400000: no relocations" "hbx_ok"
run_case "an entry point outside code"        "hbx_entry"  "hbx: entry 0x" "hbx_ok"
run_case "a missing import names both names"  "hbx_import" "hbx: nosuch.dll!missing not found" "hbx_ok"

# PE32's refusals are the same promises, made against a different header.
run_case "a PE32+ image is refused"           "pe_64"      "pe: this system is 32-bit" "hbx_ok"
run_case "a PE32 image built elsewhere"       "pe_base"    "pe: cannot load at 0x00400000: no relocations" "hbx_ok"
run_case "a PE32 entry outside code"          "pe_entry"   "pe: entry 0x" "hbx_ok"
run_case "a PE32 writable-executable section" "pe_wx"      "pe: section .text is writable and executable" "hbx_ok"
run_case "PE32 imports are not resolved yet"  "pe_import"  "pe: imports cannot be resolved yet" "hbx_ok"
run_case "a PE32 image larger than the window" "pe_nofit"   "pe: image and file do not both fit the window" "hbx_ok"

# Section 4: the image base is chosen per launch, not built in.
run_varying_case "the image base is randomised" "hbx_aslr" "HBX_ASLR 0x[0-9A-F]\{8\}" 6

# The formats M5 has to keep working.
run_case "a flat binary still runs"           "hello"      "Hello, World!"

if [ "$failures" -ne 0 ]; then
    fail "$failures loader case(s) failed"
fi

echo "  PASS  all loader cases"
exit 0
