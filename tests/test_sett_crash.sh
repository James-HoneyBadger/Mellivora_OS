#!/usr/bin/env bash
#
# test_sett_crash.sh - CONFIG.md section 10 item 1: a multi-value commit is
# whole or it did not happen.
#
# The item asks for the machine to be crashed at every phase of a multi-value
# commit and for the store to be proved either wholly old or wholly new. What
# makes that a gate rather than a claim is a judge that reads the store back
# inside the guest, after the reboot, and says one word:
#
#     SETT_STATE=old     every value is the one before the transaction
#     SETT_STATE=new     every value is the one the transaction wrote
#     SETT_STATE=mixed   some of each - the failure
#     SETT_STATE=missing a key is not in the store at all - also a failure
#
# The workload is `sett begin`, four `sett set`, `sett commit`. Kick points are
# swept across the whole window and then across the tail, where the commit's
# single image write is: the first sweep proves a crash before the commit leaves
# the old state alone, the second lands inside the write itself, where the
# previous generation in /sett.bak is the only thing that keeps the store
# readable. A final point runs past the end and requires the new state, so a
# sweep that never reached the commit cannot pass by looking consistent.
#
# Until the store wrote a change made outside a transaction, none of this could
# be primed: the seeding `sett set` read back correctly in the boot that made it
# and was gone after the restart, so every crash point reported the state the
# master image happened to carry. The priming below asserts its own write for
# that reason.
#
# Usage: tests/test_sett_crash.sh [image]
#
# Env:
#   SETT_CRASH_ITERS=8   crash points per sweep (early and tail)
#   CRASH_ALLOW_SKIP=1   exit 0 instead of failing when QEMU is missing
#
# Exits 0 when every crash point leaves the store readable and whole, 1
# otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${ROOT}/mellivora.img}"
ITERS="${SETT_CRASH_ITERS:-8}"
ALLOW_SKIP="${CRASH_ALLOW_SKIP:-0}"

fail() { printf '  FAIL  %s\n' "$*" >&2; exit 1; }

qemu="$(command -v qemu-system-x86_64 || command -v qemu-system-i386 || true)"
if [ -z "$qemu" ] && [ -x "$HOME/.local/bin/qemu-system-x86_64" ]; then
    qemu="$HOME/.local/bin/qemu-system-x86_64"
fi
if [ -z "$qemu" ]; then
    [ "$ALLOW_SKIP" = "1" ] && { echo "  SKIP  Sett crash: no QEMU found"; exit 0; }
    fail "Sett crash: no QEMU found (install qemu-system-x86, or set CRASH_ALLOW_SKIP=1)"
fi
[ -f "$IMAGE" ] || fail "Sett crash: no disk image at $IMAGE (run 'make full' first)"
[ -f "$ROOT/tools/crash_inject.py" ] || fail "Sett crash: tools/crash_inject.py is missing"
[ -f "$ROOT/tools/qemu_drive.py" ] || fail "Sett crash: tools/qemu_drive.py is missing"

work="$(mktemp -d)"
log="$(mktemp)"
trap 'rm -rf "$work"; rm -f "$log"' EXIT
base="$work/base.img"
# The sweeps each boot a copy of this base, so the primed state has to be on a
# copy too: the master image is shared with every other gate.
cp --sparse=always "$IMAGE" "$base" || fail "Sett crash: could not copy $IMAGE"

KEY='\Registry\Machine\Crash'
printf '  priming the store  '

drive() { python3 "$ROOT/tools/qemu_drive.py" "$base" "$@" 2>&1; }

if out="$(drive "sett set $KEY K1 str old" \
        --then "sett set $KEY K2 str old" \
        --then "sett set $KEY K3 str old" \
        --then "sett set $KEY K4 str old" \
        --then "sett save" \
        --expect "written to /sett.bin" --wait 15)"; then
    :
else
    echo FAIL
    printf '%s\n' "$out" | tail -20
    fail "Sett crash: the priming writes did not reach the volume"
fi

# The base is only primed if the *reboot* agrees, and that is the same judge the
# crash points use.
if out="$(drive "sett_state_test" --expect "SETT_STATE=old" --wait 15)"; then
    echo PASS
else
    echo FAIL
    printf '%s\n' "$out" | tail -20
    fail "Sett crash: the store did not hold the primed values after a reboot"
fi

workload="$(printf 'sett begin\nsett set %s K1 str new\nsett set %s K2 str new\nsett set %s K3 str new\nsett set %s K4 str new\nsett commit' \
    "$KEY" "$KEY" "$KEY" "$KEY")"

# The judge after every replay: one of the two whole states, never a mixture and
# never a missing key. A key that is not in the store at all is the same defect
# wearing a different word, so it is rejected as loudly as a mixture.
judge=(--verify sett_state_test
       --expect-any 'SETT_STATE=old|SETT_STATE=new'
       --reject 'SETT_STATE=mixed'
       --reject 'SETT_STATE=missing')
judge_final=(--verify sett_state_test
             --expect 'SETT_STATE=new'
             --reject 'SETT_STATE=mixed'
             --reject 'SETT_STATE=missing'
             --reject 'SETT_STATE=old')

status=0
old=0
new=0

sweep() {
    local name="$1" count="$2" lo="$3" hi="$4" seed="$5"
    shift 5
    printf '  %-22s %d crash points  ' "$name" "$count"
    if iters=$(QEMU="$qemu" python3 "$ROOT/tools/crash_inject.py" \
            "$base" "$workload" \
            --iterations "$count" --min-ms "$lo" --max-ms "$hi" \
            --seed "$seed" "$@" 2>&1); then
        echo PASS
    else
        echo FAIL
        printf '%s\n' "$iters" | tail -40
        status=1
    fi
    printf '%s\n' "$iters" >>"$log"
}

# The delays are measured from the first line typed, not the last, so the early
# points land *inside* the transaction - after `sett begin` and one or two sets,
# before the commit - which is the only way to observe the old state surviving a
# crash. From the last line, every point would land after the work was typed and
# the "old" side of the evidence would never appear.
sweep "inside the transaction" "$ITERS" 40 400 11 --from-start "${judge[@]}"
sweep "around the commit" "$ITERS" 400 1500 12 --from-start "${judge[@]}"
# Past the end: the transaction has committed, so the store must be new. A sweep
# that never tore the commit would otherwise pass by never seeing it.
sweep "after the commit" 1 3000 3000 1 --from-start "${judge_final[@]}"

verdicts="$(grep -o 'SETT_STATE=[a-z]*' "$log" 2>/dev/null || true)"
old="$(printf '%s\n' "$verdicts" | grep -c 'SETT_STATE=old' || true)"
new="$(printf '%s\n' "$verdicts" | grep -c 'SETT_STATE=new' || true)"

if [ "$new" -lt 1 ]; then
    printf '  FAIL  no crash point ever replayed the committed state, so the\n' >&2
    printf '        sweep never reached the commit and proved nothing about it\n' >&2
    status=1
fi

if [ "$status" -eq 0 ]; then
    printf '  PASS  %d crash points: %d replayed old, %d replayed new, none mixed\n' \
        "$(( ITERS * 2 + 1 ))" "$old" "$new"
else
    printf '  FAIL  a crash point left the store half committed or unreadable\n' >&2
fi
exit "$status"
