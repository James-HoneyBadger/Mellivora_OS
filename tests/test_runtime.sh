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

# run_case NAME CMD EXPECT [THEN] [REJECT] [MEM_MB] [SMP] [CPU] [WAIT_S] [PROFILE]
# An empty EXPECT means "only check that nothing faulted" (useful for a
# graphics program that owns the screen and prints nothing).
#
# WAIT_S is how long the driver keeps looking for EXPECT after the command
# (its default is 6 seconds). A case that forks and execs a program sixteen
# times needs a real budget: it is not slow because anything is wrong, it is
# slow because it does the work, and a window that is too small reports "not
# found" for a program that is still running.
#
# KEYS is a comma-separated list of QEMU key names pressed after the shell
# command, and WAIT_FOR is a serial marker to wait for before pressing them (the
# desktop's own "up" marker, so a keystroke cannot land on a machine that is
# still starting).
#
# MONITOR is a raw QEMU monitor command run after the shell command, for the
# things a hand presses rather than types (system_powerdown). EXIT_WAIT is how
# long QEMU is given to terminate on its own: that is how a machine that powers
# itself off is asserted, since a powered-off guest has no screen to read.
#
# The default machine is the `p4` profile from tools/machine_profiles.py: a
# Pentium 4 with SSE2, a local APIC, 512 MB and AC'97. A case overrides the
# profile when it is about the machine rather than the guest: `p4-noapic` for
# the 8259 path, `--smp 2` for the I/O APIC path. A raw CPU model override is
# still there for a one-off, but a profile is the thing to reach for.
run_case() {
    local name="$1" cmd="$2" expect="$3" then="${4:-}" reject="${5:-CPU EXCEPTION}"
    local mem="${6:-}" smp="${7:-}" cpu="${8:-}" wait_s="${9:-}" profile="${10:-}"
    local monitor="${11:-}" exit_wait="${12:-}" keys="${13:-}" wait_for="${14:-}"
    local args=("$IMAGE" "$cmd" --reject "$reject")
    if [ -n "$monitor" ]; then
        args+=(--monitor "$monitor")
    fi
    if [ -n "$exit_wait" ]; then
        args+=(--exit-wait "$exit_wait")
    fi
    if [ -n "$keys" ]; then
        args+=(--keys "$keys")
    fi
    if [ -n "$wait_for" ]; then
        args+=(--wait-for "$wait_for")
    fi
    if [ -n "$wait_s" ]; then
        args+=(--wait "$wait_s")
    fi
    if [ -n "$expect" ]; then
        args+=(--expect "$expect")
    fi
    if [ -n "$then" ]; then
        args+=(--then "$then")
    fi
    if [ -n "$mem" ]; then
        args+=(--mem "$mem")
    fi
    if [ -n "$smp" ]; then
        args+=(--smp "$smp")
    fi
    if [ -n "$cpu" ]; then
        args+=(--cpu "$cpu")
    fi
    if [ -n "$profile" ]; then
        args+=(--profile "$profile")
    fi
    # Two guests writing the same image at once is not a test result: a case
    # killed by the timeout used to leave its QEMU behind, and the next boot
    # then failed its disk writes. Say so instead of reporting a mystery.
    local stray
    stray="$(ps -eo args | grep -c '[q]emu-system')"
    if [ "$stray" -gt 0 ]; then
        echo "  WARNING: $stray QEMU process(es) already running before \"$name\""
    fi
    printf '  %-34s ' "$name"
    # A failed case keeps what the machine actually said: without it a FAIL is
    # a line with no evidence behind it.
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
# (one match per launch, in launch order) to take at least two different
# values. This is how an address that is supposed to be randomised gets tested:
# a single run cannot show the difference.
run_varying_case() {
    local name="$1" cmd="$2" pattern="$3" runs="${4:-4}"
    local out i
    out="$(mktemp)"
    local args=("$IMAGE" "$cmd" --reject "CPU EXCEPTION")
    for ((i = 1; i < runs; i++)); do
        args+=(--then "$cmd")
    done
    args+=(--out "$out")
    printf '  %-34s ' "$name"
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

echo "  runtime tests: $(basename "$IMAGE"), ${CASE_TIMEOUT}s per case"

run_case "flat binary runs"          "hello"                 "Hello, World!"
run_case "time syscalls"             "time_test"             "TIME_OK"
run_case "syscall validation suite"  "syscall_test arg"      "SYSCALL_OK"
# Every syscall that takes a caller pointer must reject an address no task
# maps instead of dereferencing it in ring 0: a supervisor fault restarts the
# shell and takes every task with it.
run_case "syscalls reject bad pointers" "badptr_test"         "BADPTR_OK"          "" "CPU EXCEPTION|PAGE FAULT|BADPTR_FAIL"
run_case "fork + exec + waitpid"     "timeout 2 hello"       "Hello, World!"
run_case "execve argv passing"       "argv_test alpha"       "EXECVE_OK"          "" "CPU EXCEPTION|EXECVE_FAIL"
run_case "VFS files + procfs"        "vfstest"               "All tests PASSED"   "" "CPU EXCEPTION|PAGE FAULT"
run_case "unified fd (socket+file)"  "sockfd_test"           "SOCKFD_OK"          "" "CPU EXCEPTION|SOCKFD_FAIL"
run_case "per-task fd views"         "fd_isolate_test"       "FDS_ISOLATED"       "" "CPU EXCEPTION|FDS_FAIL"
run_case "signal delivery + pgid"    "sigterm_test"          "SIGTERM_OK"         "" "CPU EXCEPTION|SIGTERM_FAIL"
run_case "handler returns + resumes" "sigret_test"           "SIGRET_OK"          "" "CPU EXCEPTION|SIGRET_FAIL"
run_case "foreground pgroup + tty"   "pgrp_test"             "PGRP_OK"            "" "CPU EXCEPTION|PGRP_FAIL"
run_case "copy-on-write fork"        "cow_test"              "COW_OK"             "" "CPU EXCEPTION|COW_FAIL|PAGE FAULT"
# The x87 stack is per-CPU, so two tasks doing floating point only agree on
# their own values if the scheduler saves and restores it. Both tasks check
# that the number they pushed is the number they pop back after 64 switches.
run_case "x87 state per task"         "fpu_test"              "FPU_OK"             "" "CPU EXCEPTION|FPU_FAIL"
# The save/restore itself, proved before any task exists: two distinct x87+SSE
# states are saved, the live registers disturbed, and each loaded back and
# required byte for byte. Runs on the default machine, which is a Pentium 4 and
# therefore has FXSR; a machine without it would not have booted at all.
run_case "x87+SSE image round trip"   "fpu"                   "FPU_SELFTEST_OK"    "" "CPU EXCEPTION|FPU_SELFTEST_FAIL|FPU_SELFTEST_SKIP"
# SSE state is per-task too, and is saved in the same slot with
# fxsave/fxrstor: the same two-task test as fpu_test, with XMM registers.
run_case "SSE state per task"         "sse_test"              "SSE_OK"             "" "CPU EXCEPTION|SSE_FAIL|SSE_SKIP"
# M0's C exit criterion, kernel half: the module is linked into the image at a
# fixed address, its header is checked, its init is handed the host table, and
# it verifies the freestanding runtime and the C-to-assembly call.
run_case "C kernel module"            "cmod"                  "CMOD_OK"            "" "CPU EXCEPTION|CMOD_FAIL|CMOD_ABSENT"
# M0's C exit criterion, user half: a program built by the host C compiler
# (not tcc), linked at the same 0x00500000 as every other program, using the
# same runtime in ring 3.
run_case "C user program"             "cprog"                 "C_PROG_OK"          "" "CPU EXCEPTION|C_PROG_FAIL"
# The stored mode has to deny, not decorate: root passes, the owner is held
# to the owner bits, and a stranger to the world bits.
run_case "permissions deny"           "perm_test"             "PERM_OK"            "" "CPU EXCEPTION|PERM_FAIL"
# M1's gate: an unprivileged program tries to read, write, remove, chmod and
# chown what is not its own, and passes a kernel address where a user pointer
# belongs. Every attempt must be refused, and two things it may do must still
# work - the shell runs the next case, so the machine also has to survive.
run_case "hostile program is refused" "hostile_test"          "HOSTILE_OK"         "" "CPU EXCEPTION|HOSTILE_FAIL"
# M1's object manager, first contact: a named event is created, reopened by
# name, signalled through one handle and waited on through the other.
run_case "object manager responds"  "osmoke"    "OSMOKE_OK"  "" "CPU EXCEPTION|OSMOKE_FAIL"
# M1's object conformance: rights never amplify, a stale handle and a forged
# handle are refused, the counters come back, and another session's namespace
# and a SYSTEM-integrity directory refuse a write from an ordinary user.
run_case "object model conformance" "object_test" "OBJECT_OK" "" "CPU EXCEPTION|OBJECT_FAIL"
# M1's waiting primitive: poll versus block, WaitAny by index, WaitAll, an
# auto-reset signal consumed by one waiter, and the 64-handle limit.
run_case "wait sets"                "wait_test"   "WAIT_OK"  "" "CPU EXCEPTION|WAIT_FAIL"
# The waiting primitive across tasks: in one direction the child waits and the
# parent signals, in the other the parent waits and the child signals. The child
# finds the object by name because a fork starts with an empty handle table, so
# this also proves the namespace is shared rather than per process.
run_case "cross-task wake, both ways" "crosswait_test"    "XWAIT_OK"           "" "CPU EXCEPTION|XWAIT_FAIL|PAGE FAULT" "" "" "" 15
# An open file is a typed object, not only a descriptor: SYS_OPEN papers over
# the object model exactly as OBJECTS.md §2 describes, and the counts prove the
# object is created, named after its path and released on close.
run_case "an open file is an object" "fileobj_test"        "FILEOBJ_OK"         "" "CPU EXCEPTION|FILEOBJ_FAIL"
# Quotas: creating objects in a loop stops at the ceiling instead of starving
# the slab, a close returns the charge, and the live count comes back.
run_case "object quota is enforced"  "objquota_test"       "QUOTA_OK"           "" "CPU EXCEPTION|QUOTA_FAIL" "" "" "" 150
# The two commands the object manager's spec names. Both read counters the
# kernel already keeps: `objects` the per-type live/peak counts plus the
# per-process ceiling, `handles` the per-task handle count.
run_case "objects command reports the table" "objects"   "quota 256 per process" "" "CPU EXCEPTION|Unknown command"
run_case "handles command reports totals"    "handles"   "Handles per task:"     "" "CPU EXCEPTION|Unknown command"
# A handle carries its own rights: a read-only handle cannot write back, a
# write-only one cannot be read, even as root where the mode allows both.
run_case "handle rights are enforced" "handle_test"           "HANDLE_OK"          "" "CPU EXCEPTION|HANDLE_FAIL"
# Adding a name is a write to the directory: a user who owns a directory may
# not add to it once its write bit is clear, and may not rename or truncate a
# file of somebody else's that sits inside it.
run_case "directory rights are enforced" "dirperm_test"        "DIRPERM_OK"         "" "CPU EXCEPTION|DIRPERM_FAIL"
# A bare name that is a program runs it; one that is a directory enters it.
# The trailing space matters: the tool anchors its expectation after the echoed
# command, and the prompt repeats the directory name.
run_case "folder name enters it"      "docs "                 "Lair:/docs>"        "" "Unknown command|CPU EXCEPTION|fault"
run_case "folder name enters it (module)" "Burrows "          "Lair:/Burrows>"     "" "Unknown command|CPU EXCEPTION|fault"
run_case "unknown name still errors"  "nosuchthing"           "Type 'help'"        "" "CPU EXCEPTION"
# M2 starts with the machine describing itself: the RSDP is found, the
# RSDT/XSDT walked, and the MADT read for the CPU and APIC inventory.
run_case "ACPI tables are read"       "acpi"                  "ACPI: MADT at 0x"   "" "CPU EXCEPTION|no RSDP"
# The FADT is the machine's own description of how to stop: the power blocks,
# the interrupt the SCI arrives on, and the S5 encoding read out of the DSDT.
# A machine that reports an S5 type of "not in the DSDT" cannot be powered off.
run_case "ACPI describes the power hardware" "acpi" "ACPI: S5 sleep type: 0" "" "CPU EXCEPTION|ACPI: power: the FADT|not in the DSDT"
# Soft-off: the shell's shutdown has to make the *machine* go away, not halt
# the CPU. QEMU exiting is the assertion - a guest that powers down takes the
# monitor socket with it.
run_case "shutdown powers the machine off" "shutdown" "" "" "CPU EXCEPTION" "" "" "" "" "" "" 25
# The power button is a fixed ACPI event on the SCI: the monitor presses the
# same button a hand would, and the machine has to notice and shut down.
run_case "the power button shuts down" "help" "POWER: button pressed" "" "CPU EXCEPTION" "" "" "" "" "" "system_powerdown" 25
# Quotas: the ledger is derived from the tree at mount, a write past the
# ceiling is refused, and raising the ceiling lets it through. The numbers come
# from the image, so the cases use the extreme (a ceiling of one block) rather
# than reciting a figure that changes whenever a program is added.
run_case "the quota ledger is derived from the tree" "quota" "QUOTA_OK" "" "CPU EXCEPTION"
run_case "a quota refuses a write at the ceiling" "quota set 0 1" "Write error" "touch /overq" "CPU EXCEPTION|Saved."
run_case "raising the ceiling allows the write" "quota set 0 4000" "Saved." "touch /overq" "CPU EXCEPTION|Write error"
run_case "the ledger rebuilds on demand" "quota rebuild" "ledger rebuilt" "" "CPU EXCEPTION"
# The volume manager reads the partition table instead of assuming where the
# filesystem starts, and probes the volume it finds for a superblock.
run_case "the volume manager reads the table" "volumes" "Volumes: 1  table: MBR" "" "CPU EXCEPTION"
run_case "a volume reports its filesystem" "volumes" "HBFS at +8481" "" "CPU EXCEPTION|no filesystem"
# The driver model: every driver registered, bound to the device it serves, and
# the ones this machine does not have reported as absent rather than silent.
run_case "the driver model binds the machine" "drivers" "10 bound, 0 conflicts" "" "CPU EXCEPTION"
run_case "a driver names its device" "drivers" "rtl8139     bound pci" "" "CPU EXCEPTION"
# The boot log keeps what the boot path said, in order, with the processor.
run_case "the boot log records the stages" "bootlog" "drv ata: bound" "" "CPU EXCEPTION"
# The ring holds more than a screen. `bootlog` says each line on the screen and
# on COM1, so the early records are readable from outside the guest even though
# the screen has scrolled past them.
run_case "the boot log has the early phases" "bootlog" "cpu0 I drv serial: bound" "" "CPU EXCEPTION"
# The desktop reaches the same power paths: Ctrl+Esc, six items down to "Shut
# Down...", Enter. The dialog is a window like any other, so opening it is
# visible and cancelling it leaves the machine running.
run_case "desktop opens the shutdown dialog" "burrows" "[burrows] shutdown dialog" "" "CPU EXCEPTION|PAGE FAULT" "" "" "" "" "" "" "" "ctrl-esc,down,down,down,down,down,down,ret" "[burrows] desktop up"
# ...and Enter on its default button powers the machine off, which is asserted
# by QEMU itself going away.
run_case "desktop shuts the machine down" "burrows" "[burrows] shutdown dialog" "" "CPU EXCEPTION|PAGE FAULT" "" "" "" "" "" "" 25 "ctrl-esc,down,down,down,down,down,down,ret,ret" "[burrows] desktop up"
# ...and the count is the firmware's: the same image on a 2-CPU machine must
# report 2. The assertion is on the command's output rather than the boot line:
# an expectation is matched after the echoed command, and the boot line is
# printed before it.
run_case "ACPI counts the CPUs"       "acpi"                  "ACPI: CPUs: 2"      "" "CPU EXCEPTION|ACPI: CPUs: 1" "" 2
# A part with no local APIC keeps the 8259 in charge and the PIT on the tick:
# the firmware described an APIC the CPU does not have, and the probe is what
# decides. This used to be the default 486; now that the kernel refuses a 486,
# the 8259 path is reached through the p4-noapic profile, which is the same
# Pentium 4 with APIC removed from CPUID.
run_case "no APIC means the 8259"     "acpi"                  "8259 (no usable local APIC)" "" "CPU EXCEPTION|I/O APIC\\|no RSDP" "" "" "" "" p4-noapic
# With a local APIC the tick moves to its timer, measured against the PIT
# because nothing states the APIC timer's frequency.
run_case "tick moves to the APIC timer" "acpi"                "tick clock: local APIC timer" "" "CPU EXCEPTION|tick clock: PIT" "" 2
# ...and it really ticks: this program sleeps on tick counts and waits for
# children, so a clock that does not fire would hang here.
run_case "SMP machine sleeps on its timer" "hostile_test"      "HOSTILE_OK"         "" "CPU EXCEPTION|HOSTILE_FAIL" "" 2
# The interrupt controller follows the hardware, not the table: this CPU has a
# local APIC, so the 8259s are masked and the I/O APIC routes every line.
run_case "APIC is used when the CPU has one" "acpi"           "ACPI: interrupts: I/O APIC" "" "CPU EXCEPTION|8259 (no" "" 2
# ...and that is what makes an SMP machine usable: before this, a machine with
# two CPUs received no interrupts at all, so nothing typed was ever read.
run_case "SMP machine takes input"    "handle_test"           "HANDLE_OK"          "" "CPU EXCEPTION|HANDLE_FAIL" "" 2
# The lock-order gate refuses an inversion. It runs at boot, and again here on
# demand, so the gate itself is a thing that can be tested rather than trusted.
run_case "lock order is enforced"     "locks"                 "LOCK_SELFTEST_OK"   "" "CPU EXCEPTION|LOCK_SELFTEST_FAIL" "" 2
# All of them, not just the second: every processor the firmware describes is
# woken with INIT-SIPI-SIPI and must check in.
run_case "every described CPU comes online" "acpi"             "processors online: 4 of 4" "" "CPU EXCEPTION|online: 3 of|online: 2 of" "" 4
# Line routing, not just the tick: the network stack's interrupts arrive
# through an I/O APIC entry programmed level-triggered active-low.
run_case "SMP machine serves network" "dhcp"                  "ANSWER SECTION"     "dig example.com" "CPU EXCEPTION" "" 2
# Journal-heavy work takes seconds, and the harness starts its clock 1.5 s
# after typing: a 6 s default made these two cases fail on jitter alone.
run_case "HBFS metadata writes"      "fs_meta_test"          "FS_META_OK"         "" "CPU EXCEPTION|FS_META_FAIL" "" "" "" 30
run_case "HBFS metadata churn"       "fs_mkdir_test"         "CRASH_LOOP_OK"      "" "CPU EXCEPTION|CRASH_LOOP_FAIL" "" "" "" 30
# A path is a name plus a place. `touch /dir/name` must land in /dir - taking
# the whole string as the name once created entries nothing could look up.
run_case "touch writes into a path"  "mkdir /pt_paths"       "Saved."             "touch /pt_paths/a.txt" "CPU EXCEPTION|No such dir"
run_case "ls takes a path"           "ls /pt_paths"          "a.txt"               "" "CPU EXCEPTION|No such dir"
run_case "del takes a path and keeps flags" "touch /pt_paths/a.txt" "deleted: a.txt" "del -v /pt_paths/a.txt" "CPU EXCEPTION|No such dir"
# Virtual directories have no blocks behind them; acting on an entry there has
# to be refused, and the refusal must not disturb the mounted volume's cache.
run_case "virtual dirs refuse writes" "mkdir /tmp/nope"      "Virtual dirs are read-only" "" "CPU EXCEPTION"
run_case "a refused path leaves the volume intact" "del /tmp/*.log" "Virtual dirs are read-only" "ls /pt_paths" "CPU EXCEPTION|No such dir"
run_case "del takes a plain path"    "touch /pt_paths/b.txt"  "Deleted."           "del /pt_paths/b.txt" "CPU EXCEPTION|No such dir"
# An ACL, where a file has one, decides instead of the mode word: a named user
# writes a 0600 file, the mask narrows that grant, the owner is held to their
# own ACE, and root still passes. Self-cleaning: it deletes its fixture.
run_case "access control list conformance" "acl_test"          "ACL_OK"             "" "CPU EXCEPTION|ACL_FAIL" "" "" "" 30
run_case "acl command reports its usage"   "acl"               "acl show <f>"    "" "CPU EXCEPTION"
run_case "background job reaped"     "hello &"               "No background jobs" "ps"
run_case "DHCP + DNS"                "dhcp"                  "ANSWER SECTION"     "dig example.com"
run_case "VBE framebuffer + font"    "fonttest"              ""                   "" "CPU EXCEPTION|PAGE FAULT"
run_case "sbrk heap growth"           "sbrk_test"             "SBRK_BIG_OK"        "" "CPU EXCEPTION|PAGE FAULT|Test FAILED"
run_case "user/kernel memory isolation" "memprot_test"        "MEMPROT_OK"         "" "CPU EXCEPTION|MEMPROT_FAIL"
run_case "mmap/mprotect/munmap"         "vmtest"                "VM_OK"              "" "CPU EXCEPTION|VMTEST_FAIL"
run_case "demand-zero + partial munmap"  "vmtest2"               "VM2_OK"             "" "CPU EXCEPTION|VMTEST2_FAIL"
run_case "file-backed mmap"              "vmtest3"               "VM3_OK"             "" "CPU EXCEPTION|VMTEST3_FAIL"
run_case "heap on the VMM"               "heaptest"              "HEAP_OK"            "" "CPU EXCEPTION|HEAPTEST_FAIL|PAGE FAULT"
run_case "shared file mapping writes back" "vmtest4"             "VM4_OK"             "" "CPU EXCEPTION|VMTEST4_FAIL"
run_case "shared mappings are coherent"   "vmtest5"               "VM5_OK"             "" "CPU EXCEPTION|VMTEST5_FAIL"
run_case "stack guard + growth"          "stack_test"            "STACK_GROW_OK"      "" "CPU EXCEPTION|PAGE FAULT|STACK_FAIL"
# The guard page must kill the offending task and nothing else: the shell has to
# survive and run the next command.
run_case "stack overflow contained"      "stack_overflow"        "task killed"        "hello" "CPU EXCEPTION|STACK_FAIL"
# Every launch unmaps the lower 48 stack pages of the image it replaces. Their
# frames are private to that task, so dropping the mapping has to return them:
# a fork+exec loop must not walk the free-page count down.
run_case "launch does not leak frames"   "stack_leak_test"       "STACKLEAK_OK"       "" "CPU EXCEPTION|STACKLEAK_FAIL" "" "" "" 40
# A task torn down by a signal never runs its own exit path, so the kill has to
# release what it mapped - frames, page-cache references and page tables.
run_case "signal kill releases memory"   "kill_leak_test"        "KILLLEAK_OK"        "" "CPU EXCEPTION|KILLLEAK_FAIL"
# The stack top is chosen per launch, so the same command has to land in a
# different place each time it runs. exec keeps its top on purpose (it reuses
# the task's existing mapping window), so this uses real launches.
run_varying_case "stack top is randomised"  "stack_aslr_test"     "ASLR_TOP [0-9a-f]\{8\}"
# A new task's window is a copy of the shell's, so the launcher has to clear
# what the previous program left above its own image: its heap, its mmap scratch
# and its whole user stack. stale_write fills all of that with a marker and
# stale_probe looks for it at a fixed address and across the stack window.
run_case "no stale bytes from the last program" "stale_write" "STALE_WROTE_OK" "stale_probe" "CPU EXCEPTION|STALE_LEAK|fault"
# Reliability: arm a fault, do the operation, require the error path to work.
# OOM at every depth of the fork path (with the free-page count checked after
# each), a disk whose writes all fail, and a dropped network frame that the
# resolver retries past. `dhcp` first, because the network section needs the NIC.
run_case "fault injection: OOM, disk, net" "dhcp" "FAULT_OK" "fault_inject_test" "CPU EXCEPTION|FAULT_FAIL|fault"

# Above 128 MB, physical frames live beyond the boot identity map: the allocator
# walks free-list nodes that are not mapped yet, the fault handler faults on its
# own frame zeroing, and a forked share is tracked in cow_ref only if the table
# covers every frame the allocator can hand out. These cases boot a guest whose
# RAM is well past that boundary; the same programs also run on the 128 MB
# default above, so a failure here is specific to the high-memory paths.
#
# vmtest2 asks for specific hints (256 MB and 320 MB), so it needs a guest whose
# demand-zone base is below them: 192 MB is the smallest RAM above the identity
# map and still leaves the historical hints valid.
run_case "high memory: fork + COW + munmap" "vmtest"            "VM_OK"              "" "CPU EXCEPTION|VMTEST_FAIL" 384
run_case "high memory: demand-zero pages"   "vmtest2"           "VM2_OK"             "" "CPU EXCEPTION|VMTEST2_FAIL" 192
run_case "high memory: copy-on-write fork"  "cow_test"          "COW_OK"             "" "CPU EXCEPTION|COW_FAIL|PAGE FAULT" 384
run_case "high memory: heap workload"       "heaptest"          "HEAP_OK"            "" "CPU EXCEPTION|HEAPTEST_FAIL|PAGE FAULT" 384
# The file paths read a block straight into a frame that may sit above the
# identity map, so they exercise the handler's nested-fault path too.
run_case "high memory: file-backed mapping" "vmtest3"           "VM3_OK"             "" "CPU EXCEPTION|VMTEST3_FAIL" 384
run_case "high memory: shared coherence"    "vmtest5"           "VM5_OK"             "" "CPU EXCEPTION|VMTEST5_FAIL" 384

if [ "$failures" -ne 0 ]; then
    fail "$failures runtime case(s) failed"
fi

echo "  PASS  all runtime cases"
exit 0
