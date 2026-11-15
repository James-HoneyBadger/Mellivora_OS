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
        --then "sett set $KEY K5 str old" \
        --then "sett save" \
        --then "sett get $KEY K5" \
        --expect "written to /sett.bin" --expect "= old" --wait 15)"; then
    :
else
    echo FAIL
    printf '%s\n' "$out" | tail -20
    fail "Sett crash: the priming writes did not reach the volume"
fi

# The base is only primed if the *reboot* agrees, and that is the same judge the
# crash points use. It has to agree about all five: a seed that silently did not
# land would leave the sweep judging a key the base never had, and every point
# would report the same answer for the wrong reason.
if out="$(drive "sett_state_test" --expect "SETT_STATE=old" \
        --expect "SETT_ONE=old" --wait 15)"; then
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
# K5 is the single-write judge: the transaction never touches it, so a
# "missing" there means the store lost something it was not asked to change.
judge=(--verify sett_state_test
       --expect-any 'SETT_STATE=old|SETT_STATE=new'
       --reject 'SETT_STATE=mixed'
       --reject 'SETT_STATE=missing'
       --reject 'SETT_ONE=missing')
judge_final=(--verify sett_state_test
             --expect 'SETT_STATE=new'
             --reject 'SETT_STATE=mixed'
             --reject 'SETT_STATE=missing'
             --reject 'SETT_STATE=old'
             --reject 'SETT_ONE=missing')
judge_one=(--verify sett_state_test
           --expect 'SETT_STATE=old'
           --expect-any 'SETT_ONE=old|SETT_ONE=new'
           --reject 'SETT_ONE=missing')
judge_one_final=(--verify sett_state_test
                 --expect 'SETT_STATE=old'
                 --expect 'SETT_ONE=new'
                 --reject 'SETT_ONE=missing'
                 --reject 'SETT_ONE=old')

status=0
txn_old=0
txn_new=0
one_old=0
one_new=0

# sweep NAME COUNT LO HI SEED WORKLOAD TALLY [options...]
#
# TALLY is the word the summary counts for this sweep - SETT_STATE for the
# transaction sweeps, SETT_ONE for the single-write ones. Both judges print both
# words, so counting them together would report the same numbers twice and hide
# which side of a sweep the evidence came from.
#
# The verdicts are tallied as they are read, so the summary says how the crash
# points split between the two whole states. A run that only ever replayed one
# of them has not proved that the other is reachable.
sweep() {
    local name="$1" count="$2" lo="$3" hi="$4" seed="$5" work="$6" tally="$7"
    shift 7
    printf '  %-22s %d crash points  ' "$name" "$count"
    if iters=$(QEMU="$qemu" python3 "$ROOT/tools/crash_inject.py" \
            "$base" "$work" \
            --iterations "$count" --min-ms "$lo" --max-ms "$hi" \
            --seed "$seed" "$@" 2>&1); then
        echo PASS
    else
        echo FAIL
        printf '%s\n' "$iters" | tail -40
        status=1
    fi
    local o n
    o=$(printf '%s\n' "$iters" | grep -c "$tally=old" || true)
    n=$(printf '%s\n' "$iters" | grep -c "$tally=new" || true)
    if [ "$tally" = "SETT_ONE" ]; then
        one_old=$(( one_old + o ))
        one_new=$(( one_new + n ))
    else
        txn_old=$(( txn_old + o ))
        txn_new=$(( txn_new + n ))
    fi
    printf '%s\n' "$iters" >>"$log"
}

# The delays are measured from the first line typed, not the last, so the early
# points land *inside* the transaction - after `sett begin` and one or two sets,
# before the commit - which is the only way to observe the old state surviving a
# crash. From the last line, every point would land after the work was typed and
# the "old" side of the evidence would never appear.
sweep "inside the transaction" "$ITERS" 40 400 11 "$workload" SETT_STATE --from-start "${judge[@]}"
sweep "around the commit" "$ITERS" 400 1500 12 "$workload" SETT_STATE --from-start "${judge[@]}"
# Past the end: the transaction has committed, so the store must be new. A sweep
# that never tore the commit would otherwise pass by never seeing it.
sweep "after the commit" 1 3000 3000 1 "$workload" SETT_STATE --from-start "${judge_final[@]}"

# One plain `sett set` rewrites the whole image, and a kill inside that write is
# the window the temporary-and-rename write closes: the name must point at the
# old image or the new one, never at a file that never finished arriving.
one_write="$(printf 'sett set %s K5 str new' "$KEY")"
# The window is one 28KB write and it opens the instant the line is typed, so
# the points start at zero. Most of them still land after it - which is why the
# sweep reports how many of each answer it got rather than assuming both.
sweep "inside one write" "$ITERS" 0 250 13 "$one_write" SETT_ONE --from-start "${judge_one[@]}"
sweep "after one write" 1 2500 2500 1 "$one_write" SETT_ONE --from-start "${judge_one_final[@]}"

if [ "$txn_new" -lt 1 ] || [ "$one_new" -lt 1 ]; then
    printf '  FAIL  a sweep never replayed a committed state, so it did not reach\n' >&2
    printf '        the write it was meant to tear and proved nothing about it\n' >&2
    status=1
fi

if [ "$status" -eq 0 ]; then
    printf '  PASS  %d crash points: a transaction replayed old %d times and new\n' \
        "$(( ITERS * 2 + 2 ))" "$txn_old"
    printf '        %d times; one write replayed old %d and new %d; none mixed\n' \
        "$txn_new" "$one_old" "$one_new"
else
    printf '  FAIL  a crash point left the store half written or unreadable\n' >&2
fi
exit "$status"
