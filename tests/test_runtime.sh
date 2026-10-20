#!/usr/bin/env bash
#
# test_runtime.sh - QEMU runtime regression matrix for Mellivora OS.
#
# Boots the disk image headless, drives the shell over COM1 with
# tools/qemu_drive.py, and asserts that real user-mode workloads behave:
#
#   * a flat binary runs and returns to the prompt
#   * time syscalls report plausible values
#   * the syscall validation suite passes
#   * fork + exec + waitpid works inside a task (the foreground task model)
#   * a background job is reaped after it exits
#   * DHCP + DNS complete against QEMU's user-mode network
#
# Every case also rejects "CPU EXCEPTION" on COM1: the kernel recovers from
# faults by returning to the shell prompt, so a prompt alone proves nothing.
#
# Usage:  tests/test_runtime.sh [image]
#
# Env:
#   RUNTIME_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#   RUNTIME_TIMEOUT=90     per-case timeout in seconds (default 90)
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${RUNTIME_IMAGE:-$ROOT/mellivora.img}}"
CASE_TIMEOUT="${RUNTIME_TIMEOUT:-90}"
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

if ! find_qemu >/dev/null; then
    if [ "${RUNTIME_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  runtime tests: no QEMU found on this host"
        exit 0
    fi
    fail "runtime tests: no QEMU found. Install qemu-system-x86, or set RUNTIME_ALLOW_SKIP=1."
fi

[ -f "$DRIVE" ] || fail "runtime tests: missing $DRIVE"
[ -f "$IMAGE" ] || fail "runtime tests: disk image not found: $IMAGE (run 'make full' first)"

failures=0

# run_case NAME CMD EXPECT [THEN] [REJECT]
# An empty EXPECT means "only check that nothing faulted" (useful for a
# graphics program that owns the screen and prints nothing).
run_case() {
    local name="$1" cmd="$2" expect="$3" then="${4:-}" reject="${5:-CPU EXCEPTION}"
    local args=("$IMAGE" "$cmd" --reject "$reject")
    if [ -n "$expect" ]; then
        args+=(--expect "$expect")
    fi
    if [ -n "$then" ]; then
        args+=(--then "$then")
    fi
    printf '  %-34s ' "$name"
    if timeout "$CASE_TIMEOUT" python3 "$DRIVE" "${args[@]}" >/dev/null 2>&1; then
        echo "PASS"
    else
        echo "FAIL"
        failures=$((failures + 1))
    fi
}

echo "  runtime tests: $(basename "$IMAGE"), ${CASE_TIMEOUT}s per case"

run_case "flat binary runs"          "hello"                 "Hello, World!"
run_case "time syscalls"             "time_test"             "TIME_OK"
run_case "syscall validation suite"  "syscall_test arg"      "SYSCALL_OK"
run_case "fork + exec + waitpid"     "timeout 2 hello"       "Hello, World!"
run_case "execve argv passing"       "argv_test alpha"       "EXECVE_OK"          "" "CPU EXCEPTION|EXECVE_FAIL"
run_case "VFS files + procfs"        "vfstest"               "All tests PASSED"   "" "CPU EXCEPTION|PAGE FAULT"
run_case "unified fd (socket+file)"  "sockfd_test"           "SOCKFD_OK"          "" "CPU EXCEPTION|SOCKFD_FAIL"
run_case "per-task fd views"         "fd_isolate_test"       "FDS_ISOLATED"       "" "CPU EXCEPTION|FDS_FAIL"
run_case "signal delivery + pgid"    "sigterm_test"          "SIGTERM_OK"         "" "CPU EXCEPTION|SIGTERM_FAIL"
run_case "handler returns + resumes" "sigret_test"           "SIGRET_OK"          "" "CPU EXCEPTION|SIGRET_FAIL"
run_case "foreground pgroup + tty"   "pgrp_test"             "PGRP_OK"            "" "CPU EXCEPTION|PGRP_FAIL"
run_case "copy-on-write fork"        "cow_test"              "COW_OK"             "" "CPU EXCEPTION|COW_FAIL|PAGE FAULT"
run_case "HBFS metadata writes"      "fs_meta_test"          "FS_META_OK"         "" "CPU EXCEPTION|FS_META_FAIL"
run_case "HBFS metadata churn"       "fs_mkdir_test"         "CRASH_LOOP_OK"      "" "CPU EXCEPTION|CRASH_LOOP_FAIL"
run_case "background job reaped"     "hello &"               "No background jobs" "ps"
run_case "DHCP + DNS"                "dhcp"                  "ANSWER SECTION"     "dig example.com"
run_case "VBE framebuffer + font"    "fonttest"              ""                   "" "CPU EXCEPTION|PAGE FAULT"
run_case "sbrk heap growth"           "sbrk_test"             "SBRK_BIG_OK"        "" "CPU EXCEPTION|PAGE FAULT|Test FAILED"
run_case "user/kernel memory isolation" "memprot_test"        "MEMPROT_OK"         "" "CPU EXCEPTION|MEMPROT_FAIL"
run_case "mmap/mprotect/munmap"         "vmtest"                "VM_OK"              "" "CPU EXCEPTION|VMTEST_FAIL"

if [ "$failures" -ne 0 ]; then
    fail "$failures runtime case(s) failed"
fi

echo "  PASS  all runtime cases"
exit 0
