#!/usr/bin/env bash
#
# bench_guest.sh - the guest side of the Latency and Footprint gates.
#
# docs/ROADMAP.md's gate table names numbers and says `make check` must measure
# and report them. `make bench` times host make targets, so until this script
# existed nothing measured the machine under test. programs/perf_test.asm runs
# inside the guest and prints the raw counts; this compares them with a baseline
# recorded on the same machine profile.
#
# Units are TSC cycles, not microseconds, and that is a deliberate limitation:
# the TSC rate is not architectural, nothing in the guest publishes it, and the
# nanosecond clock of record belongs to M2 (docs/spec/PLATFORM.md). Measuring
# the rate against a sleep of known ticks gave 163 to 6427 cycles/us on one
# machine, which is a guess with four digits, not a conversion. The cycle counts
# themselves repeat to a few percent, so they are what a regression gate can
# stand on.
#
# Usage:
#   tests/bench_guest.sh            check against the recorded baseline
#   tests/bench_guest.sh --update   record the current numbers as the baseline
#
# Env:
#   MV_PROFILE     machine profile (default: tests/machine_profile.sh's default)
#   GUEST_ALLOW_SKIP=1  exit 0 when QEMU is missing (make check sets this)
#
# Exits 0 when every metric is within tolerance, 1 otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPDATE=0
image_arg=""
for arg in "$@"; do
    case "$arg" in
        --update) UPDATE=1 ;;
        -*) ;;
        *) [ -z "$image_arg" ] && image_arg="$arg" ;;
    esac
done
IMAGE="${image_arg:-${GUEST_IMAGE:-$ROOT/mellivora.img}}"
DRIVE="$ROOT/tools/qemu_drive.py"
BASELINE_DIR="$ROOT/benchmark"

. "$ROOT/tools/machine_profile.sh"

# Tolerances. A syscall and a yield are pure CPU work; a spawn reads the disk, so
# it is allowed more room. These are regression bounds, not targets: a number
# that improves is always accepted.
TOL_SYSCALL=${TOL_SYSCALL:-1.5}
TOL_YIELD=${TOL_YIELD:-1.5}
TOL_SPAWN=${TOL_SPAWN:-2.0}

# How many times the harness runs. The guest shares the host with whatever else
# is running, and a single sample can be two to three times slower through no
# fault of the code under test: the same tree measured 520 and then 783 cycles
# per syscall on consecutive runs. Taking the best of several measures what the
# machine can do rather than who else was awake, which is what a regression
# gate should compare. A real regression lifts every run, so the minimum
# still moves.
GUEST_RUNS=${GUEST_RUNS:-3}

# The gate, from the roadmap: < 16 MB of RAM in use when the machine is idle.
FOOTPRINT_LIMIT_KB=16384

fail() {
    printf '  FAIL  %s\n' "$*" >&2
    exit 1
}

if [ ! -f "$IMAGE" ]; then
    fail "guest benchmark: disk image not found: $IMAGE (run 'make full' first)"
fi
if ! command -v python3 >/dev/null 2>&1; then
    fail "guest benchmark: python3 is required"
fi
if ! python3 -c 'import sys; sys.exit(0)' 2>/dev/null; then
    fail "guest benchmark: python3 does not run"
fi
if [ ! -f "$DRIVE" ]; then
    fail "guest benchmark: missing $DRIVE"
fi

BASELINE="$BASELINE_DIR/guest-$MV_PROFILE.txt"
MEM_MB="$(mv_machine_mem)"

if ! mv_machine_supported; then
    # The kernel refuses this profile at boot, so there is nothing to time.
    echo "  SKIP  guest benchmark: profile '$MV_PROFILE' is below the CPU floor"
    exit 0
fi

OUT="$BASELINE_DIR/latest-guest-$MV_PROFILE.txt"
mkdir -p "$BASELINE_DIR"

echo "  guest benchmark: profile $MV_PROFILE, ${MEM_MB} MB"
echo "  image:           $IMAGE"

# The harness prints one KEY=VALUE per line and PERF_OK last. --wait covers the
# spawn loop: eight fork+exec+wait cycles plus the calibration sleep of 500 ms
# is a couple of seconds, but a cold page cache is slower.
value_in() {
    sed -n "s/.*$2=\([0-9][0-9]*\).*/\1/p" "$1" | head -1
}

best_syscall=""
best_yield=""
best_spawn=""
ticks_slept=""
free_boot=""
free_now=""
cycles_per_tick=""
runs_done=0

for run in $(seq 1 "$GUEST_RUNS"); do
    if ! timeout 180 python3 "$DRIVE" "$IMAGE" "perf_test" --wait 60 \
            --out "$OUT" >/dev/null 2>"$OUT.err"; then
        if [ "${GUEST_ALLOW_SKIP:-0}" = "1" ] && ! command -v qemu-system-x86_64 >/dev/null 2>&1 \
                && ! command -v qemu-system-i386 >/dev/null 2>&1; then
            echo "  SKIP  guest benchmark: no QEMU found on this host"
            exit 0
        fi
        cat "$OUT.err" >&2 2>/dev/null || true
        fail "guest benchmark: perf_test did not run cleanly (see $OUT.err)"
    fi

    if grep -q 'PERF_FAIL' "$OUT" 2>/dev/null; then
        fail "guest benchmark: perf_test reported $(grep -o 'PERF_FAIL[A-Z_]*' "$OUT" | head -1)"
    fi
    if ! grep -q 'PERF_OK' "$OUT" 2>/dev/null; then
        fail "guest benchmark: no PERF_OK in the guest output (see $OUT)"
    fi
    runs_done=$((runs_done + 1))

    r_ticks="$(value_in "$OUT" PERF_TICKS_SLEPT)"
    r_syscall="$(value_in "$OUT" PERF_SYSCALL_CYC)"
    r_yield="$(value_in "$OUT" PERF_YIELD_CYC)"
    r_spawn="$(value_in "$OUT" PERF_SPAWN_CYC)"
    r_free_boot="$(value_in "$OUT" PERF_FREE_PAGES_BOOT)"
    r_free_now="$(value_in "$OUT" PERF_FREE_PAGES_NOW)"
    r_per_tick="$(value_in "$OUT" PERF_CYCLES_PER_TICK)"

    for pair in r_ticks:PERF_TICKS_SLEPT r_syscall:PERF_SYSCALL_CYC \
                r_yield:PERF_YIELD_CYC r_spawn:PERF_SPAWN_CYC \
                r_free_boot:PERF_FREE_PAGES_BOOT r_free_now:PERF_FREE_PAGES_NOW \
                r_per_tick:PERF_CYCLES_PER_TICK; do
        varname="${pair%%:*}"; key="${pair##*:}"
        eval "v=\$$varname"
        [ -n "$v" ] || fail "guest benchmark: PERF output is missing $key (see $OUT)"
    done

    # The first run's footprint is the idle figure; nothing allocates after boot.
    [ -z "$free_boot" ] && free_boot="$r_free_boot"
    free_now="$r_free_now"
    [ -z "$ticks_slept" ] && ticks_slept="$r_ticks"
    [ -z "$cycles_per_tick" ] && cycles_per_tick="$r_per_tick"

    if [ -z "$best_syscall" ] || [ "$r_syscall" -lt "$best_syscall" ]; then best_syscall="$r_syscall"; fi
    if [ -z "$best_yield" ] || [ "$r_yield" -lt "$best_yield" ]; then best_yield="$r_yield"; fi
    if [ -z "$best_spawn" ] || [ "$r_spawn" -lt "$best_spawn" ]; then best_spawn="$r_spawn"; fi
done

syscall_cyc="$best_syscall"
yield_cyc="$best_yield"
spawn_cyc="$best_spawn"
echo "  samples:         $runs_done (best of each metric is compared)"

for name in ticks_slept syscall_cyc yield_cyc spawn_cyc free_boot free_now cycles_per_tick; do
    eval "v=\$$name"
    [ -n "$v" ] || fail "guest benchmark: PERF output is missing $name (see $OUT)"
done

# --- sanity: the numbers have to be numbers ------------------------------
if [ "$ticks_slept" -lt 49 ] || [ "$ticks_slept" -gt 60 ]; then
    fail "guest benchmark: calibration sleep consumed $ticks_slept ticks, expected ~50"
fi
if [ "$syscall_cyc" -lt 50 ] || [ "$syscall_cyc" -gt 200000 ]; then
    fail "guest benchmark: syscall cost $syscall_cyc cycles is not plausible"
fi
if [ "$yield_cyc" -lt "$syscall_cyc" ]; then
    fail "guest benchmark: a yield ($yield_cyc) cannot be cheaper than a syscall ($syscall_cyc)"
fi

# --- footprint -----------------------------------------------------------
# Not a baseline: the roadmap states a limit, so it is checked as one. Reserved
# is what the kernel claimed at boot (image, page tables, bitmap, tag array),
# and "now" is the same figure because the harness has allocated nothing.
reserved_kb=$(( (MEM_MB * 256 - free_boot) * 4 ))
if [ "$free_now" -gt "$free_boot" ]; then
    fail "guest benchmark: free frames grew from $free_boot to $free_now"
fi
printf '  %-34s %s KB reserved (limit %s KB)\n' "footprint" "$reserved_kb" "$FOOTPRINT_LIMIT_KB"
if [ "$reserved_kb" -gt "$FOOTPRINT_LIMIT_KB" ]; then
    fail "guest benchmark: ${reserved_kb} KB reserved exceeds the ${FOOTPRINT_LIMIT_KB} KB gate"
fi

# --- record or compare ---------------------------------------------------
if [ "$UPDATE" = "1" ] || [ ! -f "$BASELINE" ]; then
    {
        echo "# guest metrics for machine profile '$MV_PROFILE' (${MEM_MB} MB)"
        echo "# recorded by tests/bench_guest.sh --update on $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "# cycles are TSC cycles; the TSC rate is not published until M2"
        echo "ticks_slept=$ticks_slept"
        echo "cycles_per_tick=$cycles_per_tick"
        echo "syscall_cyc=$syscall_cyc"
        echo "yield_cyc=$yield_cyc"
        echo "spawn_cyc=$spawn_cyc"
        echo "free_pages_boot=$free_boot"
        echo "reserved_kb=$reserved_kb"
    } > "$BASELINE"
    echo "  baseline recorded: $BASELINE"
    [ "$UPDATE" = "1" ] && exit 0
fi

base() {
    sed -n "s/^$1=\([0-9][0-9]*\).*/\1/p" "$BASELINE" | head -1
}

failures=0
compare() {
    local label="$1" now="$2" base_key="$3" tol="$4"
    local was
    was="$(base "$base_key")"
    if [ -z "$was" ]; then
        printf '  %-34s %s (no baseline)\n' "$label" "$now"
        return 0
    fi
    # integer arithmetic: now*100 > was*tol*100
    local limit=$(( was * ${tol/./} / 10 ))
    if [ "$now" -gt "$limit" ]; then
        printf '  %-34s %s  REGRESSED (baseline %s, limit %s)\n' "$label" "$now" "$was" "$limit"
        failures=$((failures + 1))
    else
        printf '  %-34s %s  (baseline %s)\n' "$label" "$now" "$was"
    fi
}

compare "syscall cycles" "$syscall_cyc" syscall_cyc "$TOL_SYSCALL"
compare "yield cycles" "$yield_cyc" yield_cyc "$TOL_YIELD"
compare "fork+exec+wait cycles" "$spawn_cyc" spawn_cyc "$TOL_SPAWN"

if [ "$failures" -ne 0 ]; then
    fail "guest benchmark: $failures metric(s) regressed against $BASELINE"
fi

echo "  PASS  guest latency and footprint within baseline ($MV_PROFILE)"
