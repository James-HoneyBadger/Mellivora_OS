#!/bin/sh
# find_c_toolchain.sh - find a compiler that can build 32-bit freestanding C.
#
# New kernel subsystems (roadmap M1 onwards) are written in C, while the boot
# path, entry stubs, context switch and the existing modules stay in NASM. A
# compiler that produces the wrong architecture is worse than no compiler: a
# native gcc on an x86-64 host builds happily and links objects the 32-bit
# kernel cannot call. So every candidate is proved by compiling a probe and
# reading the object header back.
#
# Usage:
#   sh tools/find_c_toolchain.sh          # report the compiler and linker found
#   sh tools/find_c_toolchain.sh --make   # emit make variable assignments
#   sh tools/find_c_toolchain.sh --require  # exit non-zero when there is none
#
# Output is one line per role: "cc: <command>" and "ld: <command>". --make
# prints "C_CC := <command>" and "C_LD := <command>" instead, so the Makefile
# can $(eval) it without parsing prose.

set -u

MODE="report"
case "${1:-}" in
    --make) MODE="make" ;;
esac

PROBE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/mellivora-c-probe.XXXXXX")
trap 'rm -rf "$PROBE_DIR"' EXIT INT TERM

cat > "$PROBE_DIR/probe.c" <<'EOF'
typedef unsigned int u32;
static u32 add(u32 a, u32 b) { return a + b; }
/* cmov appears in the object only when the target really is P6-or-later, which
   is the cheapest way to see that -march=pentium4 was honoured. */
u32 lm_probe(u32 a, u32 b) { return a < b ? add(a, 1u) : add(b, 2u); }
EOF

# Keep in step with C_CFLAGS in the Makefile: this is the flag set the kernel
# module and the C user program are built with.
PROBE_CFLAGS="-O2 -std=c11 -ffreestanding -fno-builtin -fno-stack-protector \
-fno-pic -fno-pie -march=pentium4 -mtune=pentium4 -mno-sse -mno-mmx -msoft-float"

# True when the named candidates build a 32-bit x86 object.
try_cc() {
    command=$1
    # shellcheck disable=SC2086
    $command -c $PROBE_CFLAGS -o "$PROBE_DIR/probe.o" "$PROBE_DIR/probe.c" \
        >/dev/null 2>&1 || return 1
    readelf -h "$PROBE_DIR/probe.o" 2>/dev/null \
        | grep -q 'Class:.*ELF32' || return 1
    readelf -h "$PROBE_DIR/probe.o" 2>/dev/null \
        | grep -q 'Machine:.*80386' || return 1
    return 0
}

# A matching linker: ld must accept the probe object and emit a 32-bit binary.
try_ld() {
    command=$1
    # shellcheck disable=SC2086
    $command -m elf_i386 -o "$PROBE_DIR/probe.elf" "$PROBE_DIR/probe.o" >/dev/null 2>&1 || return 1
    readelf -h "$PROBE_DIR/probe.elf" 2>/dev/null \
        | grep -q 'Class:.*ELF32' || return 1
}

CC_CANDIDATES=${CC_CANDIDATES:-"${CC_C:-} i686-elf-gcc i386-elf-gcc gcc clang"}
LD_CANDIDATES=${LD_CANDIDATES:-"${LD_C:-} i686-elf-ld i386-elf-ld ld ld.lld"}

CC_FOUND=""
for candidate in $CC_CANDIDATES; do
    [ -n "$candidate" ] || continue
    command -v "${candidate%% *}" >/dev/null 2>&1 || continue
    case "$candidate" in
        gcc|clang) attempt="$candidate -m32" ;;
        *)         attempt="$candidate" ;;
    esac
    if try_cc "$attempt"; then
        CC_FOUND="$attempt"
        break
    fi
done

if [ -z "$CC_FOUND" ]; then
    # clang can target i386 without multilib installed.
    if command -v clang >/dev/null 2>&1 && try_cc "clang --target=i386-elf"; then
        CC_FOUND="clang --target=i386-elf"
    fi
fi

LD_FOUND=""
for candidate in $LD_CANDIDATES; do
    [ -n "$candidate" ] || continue
    command -v "${candidate%% *}" >/dev/null 2>&1 || continue
    if try_ld "$candidate"; then
        LD_FOUND="$candidate"
        break
    fi
done

if [ -n "$CC_FOUND" ]; then
    if [ "$MODE" = "make" ]; then
        echo "C_CC := $CC_FOUND"
        echo "C_LD := ${LD_FOUND:-none (the C probe object linked with none of: $LD_CANDIDATES)}"
    else
        echo "cc: $CC_FOUND"
        echo "ld: ${LD_FOUND:-none (the C probe object linked with none of: $LD_CANDIDATES)}"
    fi
    exit 0
fi

if [ "$MODE" = "make" ]; then
    # Empty assignments: make treats them as "not found" and prints its own
    # guidance, which belongs where the build fails rather than in a probe.
    echo "C_CC :="
    echo "C_LD :="
else
    echo "cc: none"
    echo "  Tried: $(echo $CC_CANDIDATES | tr -s ' ')"
    echo "  Install one of: gcc-multilib (x86 host), i686-elf-gcc (a cross"
    echo "  compiler, which is the only route on an ARM host), or clang"
    echo "  (which can target i386-elf without multilib)."
fi
if [ "${1:-}" = "--require" ]; then
    exit 1
fi
exit 0
