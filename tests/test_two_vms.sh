#!/usr/bin/env bash
#
# test_two_vms.sh - the M7 exit criterion, on two machines.
#
# Every other share test runs both ends of the protocol in one kernel over the
# loopback interface, which proves the protocol and not the wire. This gate
# boots two QEMU machines joined by a virtual Ethernet segment (a TCP socket
# netdev pair, no host bridge and no root), gives them addresses on the same
# subnet, and does what docs/ROADMAP.md asks for at the end of M7:
#
#   share a folder, mount it on the other, log on across the network, print to
#   a shared printer, and restart a service on the other machine.
#
# The RPC steps are the one thing on this wire that is not a file operation:
# machine B changes a Sett value on machine A through the configuration
# interface, and A then reads the value out of its own registry - which is the
# only way to tell a reply that echoed the argument from one the far machine
# actually applied.
#
# It also proves the permission model on the wire: a read-only export refuses a
# write that the writable one accepts, and a wrong secret loses the session.
#
# And it is the only place the firewall can be proved. The contract exempts
# loopback, so a single machine cannot show a blocked connection: machine B is
# refused with no rule for 4450, A counts the refusal, `net fw add tcp 4450`
# admits the next attempt, and deleting the rule refuses it again.
#
# The image is opened with -snapshot by each machine, so the run leaves it
# untouched and the two guests cannot see each other's local writes.
#
# Usage:  tests/test_two_vms.sh [image]
#
# Env:
#   TWOVM_ALLOW_SKIP=1    exit 0 instead of failing when QEMU is missing
#   TWOVM_TIMEOUT=180     seconds allowed for a machine to reach its shell
#
# Exits 0 on success, 1 on failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-${TWOVM_IMAGE:-$ROOT/mellivora.img}}"
TOOL="$ROOT/tools/qemu_twonode.py"
BOOT_TIMEOUT="${TWOVM_TIMEOUT:-180}"

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
    if [ "${TWOVM_ALLOW_SKIP:-0}" = "1" ]; then
        echo "  SKIP  two-VM tests: no QEMU found on this host"
        exit 0
    fi
    fail "two-VM tests: no QEMU found. Install qemu-system-x86, or set TWOVM_ALLOW_SKIP=1."
fi

[ -f "$TOOL" ] || fail "two-VM tests: missing $TOOL"
[ -f "$IMAGE" ] || fail "two-VM tests: disk image not found: $IMAGE (run 'make full' first)"

PLAN="$(cat <<'JSON'
{
  "boot_timeout": 180,
  "step_timeout": 30,
  "steps": [
    {"node": "a", "run": "account del alice", "name": "[a] clear any old account"},
    {"node": "a", "run": "account add alice secret1",
     "expect": ["account: added alice"]},
    {"node": "a", "run": "ifconfig 10.0.0.1",
     "expect": ["IP set to 10.0.0.1"]},
    {"node": "a", "run": "net share /rw docs /docs s3cret",
     "expect": ["Shared: docs = /docs"]},
    {"node": "a", "run": "net share ro /docs ros3cret",
     "expect": ["Shared: ro = /docs"]},
    {"node": "a", "run": "sc delete pt",
     "name": "[a] clear any old service record"},
    {"node": "a", "run": "sc create pt /bin/svcrun manual",
     "expect": ["sc: created pt"]},

    {"node": "a", "run": "net fw",
     "name": "[a] the firewall is on, and has no rule for 4450",
     "expect": ["Firewall: on", "Rules: 0"]},

    {"node": "b", "run": "ifconfig 10.0.0.2",
     "expect": ["IP set to 10.0.0.2"]},
    {"node": "b", "run": "net view 10.0.0.1",
     "name": "[b] refused: the default is inbound deny",
     "expect": ["cannot reach the share service"], "reject": ["docs"],
     "timeout": 60},
    {"node": "a", "run": "net fw",
     "name": "[a] the refusal is counted",
     "expect": ["Blocked: 1"]},
    {"node": "a", "run": "net fw add tcp 4450",
     "expect": ["fw: rule 0"]},
    {"node": "b", "run": "net view 10.0.0.1",
     "expect": ["Shares on 10.0.0.1:4450", "docs", "ro"], "timeout": 40},
    {"node": "a", "run": "net fw",
     "name": "[a] one refusal, and the rule admitted the rest",
     "expect": ["Blocked: 1"]},
    {"node": "b", "run": "net use 10.0.0.1 docs /mnt s3cret",
     "expect": ["Mounted /mnt from docs"], "timeout": 40},
    {"node": "b", "run": "dir /mnt", "expect": ["readme.txt"], "timeout": 40},
    {"node": "b", "run": "cat /mnt/todo.txt",
     "expect": ["[x] Shell tab completion"], "timeout": 40},

    {"node": "b", "run": "net cfg 10.0.0.1 docs set \\Registry\\Machine\\RpcRemote\\Proof over-the-wire s3cret",
     "expect": ["net: set \\Registry\\Machine\\RpcRemote\\Proof"], "timeout": 40},
    {"node": "b", "run": "net cfg 10.0.0.1 docs get \\Registry\\Machine\\RpcRemote\\Proof s3cret",
     "expect": ["= over-the-wire"], "timeout": 40},
    {"node": "b", "run": "net cfg 10.0.0.1 docs get \\Registry\\Machine\\RpcRemote\\Absent s3cret",
     "expect": ["status 8"], "timeout": 40},
    {"node": "b", "run": "net cfg 10.0.0.1 docs get \\Registry\\Machine\\RpcRemote\\Proof wrongsecret",
     "expect": ["(wrong secret?)"], "timeout": 40},
    {"node": "b", "run": "net log 10.0.0.1 docs s3cret",
     "expect": ["Event log: the last ", "records"], "timeout": 40},

    {"node": "b", "run": "del /btest.txt", "name": "[b] clear the local fixture"},
    {"node": "b", "run": "echo two-vm-proof > btest.txt",
     "name": "[b] write a local file with a name only this machine has"},
    {"node": "b", "run": "net put 10.0.0.1 docs /btest.txt s3cret",
     "expect": ["Wrote ", "bytes to the share"], "timeout": 40},
    {"node": "b", "run": "net put 10.0.0.1 ro /btest.txt ros3cret",
     "expect": ["refused the write"], "timeout": 40},
    {"node": "b", "run": "net use 10.0.0.1 docs wrongsecret",
     "expect": ["dropped the session"], "timeout": 40},

    {"node": "b", "run": "net logon 10.0.0.1 alice secret1",
     "expect": ["Logged on as alice on 10.0.0.1"], "timeout": 40},
    {"node": "b", "run": "net logon 10.0.0.1 alice wrongpw",
     "expect": ["refused the logon"], "timeout": 40},
    {"node": "b", "run": "net svc 10.0.0.1 docs pt start s3cret",
     "expect": ["Service pt is running"], "timeout": 60},
    {"node": "b", "run": "net print 10.0.0.1 docs /btest.txt s3cret",
     "expect": ["Queued job "], "timeout": 40},

    {"node": "a", "run": "cat /docs/btest.txt", "expect": ["two-vm-proof"]},
    {"node": "a", "run": "sett get \\Registry\\Machine\\RpcRemote Proof",
     "expect": ["= over-the-wire"]},
    {"node": "a", "run": "dir /docs", "expect": ["btest.txt"]},
    {"node": "a", "run": "printq", "expect": ["Spool-btest.txt"]},

    {"node": "a", "run": "sc stop pt",
     "name": "[a] stop the service before the overlay is thrown away"},
    {"node": "a", "run": "printflush", "name": "[a] flush the queue"},
    {"node": "a", "run": "del /Spool-btest.txt", "name": "[a] drop the spool file"},
    {"node": "a", "run": "del /docs/btest.txt", "name": "[a] drop the received file"},
    {"node": "a", "run": "sett delkey \\Registry\\Machine\\RpcRemote",
     "name": "[a] drop the key written from across the wire"},
    {"node": "a", "run": "net fw del 0",
     "expect": ["fw: rule 0 removed"]},
    {"node": "a", "run": "net share -d docs", "expect": ["Share removed"]},
    {"node": "a", "run": "net share -d ro", "expect": ["Share removed"]},
    {"node": "a", "run": "account del alice", "expect": ["account: deleted"]},
    {"node": "b", "run": "del /btest.txt", "name": "[b] drop the sent file"}
  ]
}
JSON
)"

printf '%s\n' "$PLAN" | python3 "$TOOL" --image "$IMAGE" --boot-timeout "$BOOT_TIMEOUT" \
    || fail "the two-machine workflow did not complete (transcripts above)"

echo "TWO_VM_OK"
