#!/bin/sh
# find_lm_toolchain.sh - pick a C toolchain that builds the x86-64 nucleus
#
# The long-mode nucleus (roadmap P2) is the first C code in the tree, so the
# build has to work on hosts that are not x86-64 and without a hand-built cross
# compiler. Candidates, in order:
#
#   1. $LM_CC / $LM_LD, when the caller set them
#   2. x86_64-elf-gcc + x86_64-elf-ld   (the canonical cross toolchain)
#   3. clang --target=x86_64-elf + ld.lld (one binary, cross-compiles anywhere)
#   4. the host gcc + ld, when the host is already x86-64
#
# Each candidate is *probed* - a one-line object is compiled and its ELF header
# is checked for e_machine 62 (EM_X86_64) - because a native gcc on an ARM host
# compiles happily and produces the wrong architecture.
#
# Usage: find_lm_toolchain.sh cc|ld
# Prints the command to use (possibly with flags) and exits 0, or prints
# nothing and exits 1.

set -u

what="${1:-cc}"
tmp="$(mktemp -d)" || exit 1
trap 'rm -rf "$tmp"' EXIT INT HUP TERM

cat > "$tmp/probe.c" <<'EOF'
void probe_symbol(void);
void probe_symbol(void) {}
EOF

CFLAGS_X64="-std=c11 -m64 -ffreestanding -fno-builtin -fno-pic -fno-pie \
-mno-red-zone -mcmodel=kernel -fno-asynchronous-unwind-tables \
-fno-unwind-tables -c"

# is_x86_64_object FILE -> 0 when e_machine == 62
is_x86_64_object() {
    [ -s "$1" ] || return 1
    machine=$(od -An -tx1 -j 18 -N 2 "$1" 2>/dev/null | tr -d ' \n')
    [ "$machine" = "3e00" ]
}

# probe_cc CMD... -> 0 when CMD compiles an x86-64 object
probe_cc() {
    # shellcheck disable=SC2086
    $* $CFLAGS_X64 "$tmp/probe.c" -o "$tmp/probe.o" >/dev/null 2>&1 || return 1
    is_x86_64_object "$tmp/probe.o"
}

case "$what" in
cc)
    if [ -n "${LM_CC:-}" ] && probe_cc "$LM_CC"; then
        printf '%s' "$LM_CC"
        exit 0
    fi
    if command -v x86_64-elf-gcc >/dev/null 2>&1 && probe_cc x86_64-elf-gcc; then
        printf 'x86_64-elf-gcc'
        exit 0
    fi
    if command -v clang >/dev/null 2>&1 && probe_cc "clang --target=x86_64-elf"; then
        printf 'clang --target=x86_64-elf'
        exit 0
    fi
    if command -v gcc >/dev/null 2>&1 && probe_cc gcc; then
        printf 'gcc'
        exit 0
    fi
    ;;
ld)
    if [ -n "${LM_LD:-}" ]; then
        printf '%s' "$LM_LD"
        exit 0
    fi
    if command -v x86_64-elf-ld >/dev/null 2>&1; then
        printf 'x86_64-elf-ld'
        exit 0
    fi
    if command -v ld.lld >/dev/null 2>&1; then
        printf 'ld.lld -m elf_x86_64'
        exit 0
    fi
    # A native GNU ld only works when the host is x86-64; ask it.
    if command -v ld >/dev/null 2>&1 &&
       ld -m elf_x86_64 --verbose >/dev/null 2>&1; then
        printf 'ld -m elf_x86_64'
        exit 0
    fi
    ;;
*)
    echo "usage: $0 cc|ld" >&2
    exit 2
    ;;
esac

exit 1
