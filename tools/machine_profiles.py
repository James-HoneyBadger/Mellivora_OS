#!/usr/bin/env python3
"""The emulated machine Mellivora is built and tested on.

One definition, used by the Makefile, every shell test, every Python driver and
the installers, so `make run`, `make check` and a hand-run QEMU all describe the
same computer.

The default profile is a Pentium 4 desktop of 2002: a Netburst CPU with SSE2,
512 MB of RAM, IDE, AC'97 audio, USB and an RTL8139 NIC. QEMU has no `pentium4`
CPU model, so the profile starts from the closest model and overrides CPUID to
what the real part reported: Family 15, Model 2 (Northwood), and the brand
string a 2.4 GHz P4 put in leaf 80000002H..80000004H. `make check` asserts the
guest sees exactly that, which is why the override is data here and not a
comment in a test script.

The `i486` profile is kept for A/B work on old-hardware behaviour. It is *not*
supported by the kernel any more: booting it must stop with
CPU_UNSUPPORTED on COM1, and a test asserts that it does.

Usage:
  tools/machine_profiles.py --cpu                  print the CPU model string
  tools/machine_profiles.py --mem                  print the RAM in MB
  tools/machine_profiles.py --qemu-flags           print "-cpu .. -m .." as one line
  tools/machine_profiles.py --qemu-args            print one argument per line
                                                   (--audio BACKEND picks the
                                                   host audio backend, default
                                                   none so CI and a desktop use
                                                   the same machine)
  tools/machine_profiles.py --json                 print the whole profile
  tools/machine_profiles.py --list                 print the profile names

The profile is chosen with --profile, or $MV_PROFILE, or defaults to p4.
"""

import json
import os
import shlex
import sys

BRAND_P4 = "Intel(R) Pentium(R) 4 CPU 2.40GHz"

# A Northwood Pentium 4, as CPUID describes it: family 15, model 2, stepping 7,
# and the brand string the 2.4 GHz part carried.
#
# QEMU has no `pentium4` model, so this starts from `qemu32` - the generic
# 32-bit model, which has SSE2 and the extended CPUID leaves a brand string
# needs - and overrides family/model/stepping/model-id. `pentium3` looks like a
# closer starting point and is not: it has no SSE2 (the kernel refuses it, which
# is how this was found) and no extended leaves, so the guest would report
# "(no brand string)".
CPU_P4 = "qemu32,family=15,model=2,stepping=7,model-id=" + BRAND_P4

PROFILES = {
    "p4": {
        "description": "Pentium 4 desktop (2002): Netburst, SSE2, 512 MB, AC'97",
        "cpu": CPU_P4,
        "mem": 512,
        "machine": "pc",
        # AC'97 is the P4-era codec; the ISA Sound Blaster went with the 486.
        "sound": "AC97",
        # IDE disks, USB 1.1, and the RTL8139 that every cheap 2002 NIC was.
        "devices": [
            "-usb",
            "-netdev", "user,id=net0",
            "-device", "rtl8139,netdev=net0",
        ],
        "cpu_expect": {
            "family": 15,
            "model": 2,
            "stepping": 7,
            "brand": BRAND_P4,
            "sse2": True,
        },
        "supported": True,
    },
    "p4-noapic": {
        "description": "Pentium 4 with the local APIC disabled: the 8259 path",
        # Same part as `p4`, with APIC turned off in CPUID. The interrupt
        # controller follows the hardware, so this is the machine that proves
        # the 8259 fallback still works - reachable code that nothing else
        # exercises now that the kernel refuses to boot on a 486.
        "cpu": "qemu32,family=15,model=2,stepping=7,-apic,model-id=" + BRAND_P4,
        "mem": 512,
        "machine": "pc",
        "sound": "AC97",
        "devices": [
            "-usb",
            "-netdev", "user,id=net0",
            "-device", "rtl8139,netdev=net0",
        ],
        "cpu_expect": {
            "family": 15,
            "model": 2,
            "stepping": 7,
            "brand": BRAND_P4,
            "sse2": True,
            "uses_apic": False,
        },
        "supported": True,
    },
    "i486": {
        "description": "486 (legacy): below the kernel's floor, must be refused",
        "cpu": "486",
        "mem": 128,
        "machine": "pc,pcspk-audiodev=snd0,usb=off",
        "sound": "sb16",
        "devices": [
            "-netdev", "user,id=net0",
            "-device", "rtl8139,netdev=net0",
        ],
        "cpu_expect": {"family": 4, "sse2": False},
        "supported": False,
    },
}

DEFAULT = "p4"


def _opt(argv, name):
    for i, arg in enumerate(argv):
        if arg == name and i + 1 < len(argv):
            return argv[i + 1]
        if arg.startswith(name + "="):
            return arg.split("=", 1)[1]
    return None


def resolve_profile(argv):
    for i, arg in enumerate(argv):
        if arg == "--profile" and i + 1 < len(argv):
            return argv[i + 1]
        if arg.startswith("--profile="):
            return arg.split("=", 1)[1]
    return os.environ.get("MV_PROFILE", DEFAULT)


def profile(name=None):
    name = name or os.environ.get("MV_PROFILE", DEFAULT)
    if name not in PROFILES:
        raise SystemExit(
            f"unknown machine profile {name!r}; known: {', '.join(PROFILES)}"
        )
    p = dict(PROFILES[name])
    p["name"] = name
    return p


def qemu_args(name=None, audio=None):
    """The machine half of a QEMU command line, as a list.

    `audio` is the *host* backend (pa, alsa, coreaudio, none). It defaults to
    none so a test machine is silent and a desktop passes its own: the guest
    sound device belongs to the profile, the backend does not.
    """
    p = profile(name)
    audio = audio or os.environ.get("MV_AUDIO", "none")
    args = ["-cpu", p["cpu"], "-m", str(p["mem"])]
    if p["machine"]:
        args += ["-machine", p["machine"]]
    args += ["-audiodev", f"{audio},id=snd0", "-device", f"{p['sound']},audiodev=snd0"]
    return args + list(p["devices"])


def qemu_flags(name=None, audio=None):
    """The same thing as one shell-quoted string, for `make` and echo."""
    return " ".join(shlex.quote(a) for a in qemu_args(name, audio))


def main(argv):
    name = resolve_profile(argv)
    if "--list" in argv:
        for key, p in PROFILES.items():
            mark = "" if p["supported"] else "  [below the kernel's floor]"
            print(f"{key:6} {p['description']}{mark}")
        return 0
    if "--json" in argv:
        print(json.dumps(profile(name), indent=2))
        return 0
    if "--cpu-expect" in argv:
        print(json.dumps(profile(name)["cpu_expect"]))
        return 0
    if "--qemu-args" in argv:
        for arg in qemu_args(name, _opt(argv, "--audio")):
            print(arg)
        return 0
    if "--qemu-flags" in argv:
        print(qemu_flags(name, _opt(argv, "--audio")))
        return 0
    if "--cpu" in argv:
        print(profile(name)["cpu"])
        return 0
    if "--mem" in argv:
        print(profile(name)["mem"])
        return 0
    if "--supported" in argv:
        return 0 if profile(name)["supported"] else 1
    print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
