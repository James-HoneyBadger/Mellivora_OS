#!/bin/sh
# The emulated machine, for shell scripts.
#
# The definition lives in tools/machine_profiles.py so the Makefile, the shell
# tests and the Python drivers cannot drift apart. Source this file and use:
#
#   mv_machine_args          -> fills MV_MACHINE_ARGS with one flag per element
#   mv_machine_flags         -> the same thing as one shell-quoted string
#   mv_machine_cpu           -> the -cpu model alone ('' when the profile omits it)
#   mv_machine_mem           -> RAM in MB
#   mv_machine_supported     -> 0 when the kernel runs on this profile
#
# The profile is $MV_PROFILE, defaulting to p4.

# Where this file is. bash reports a sourced file's own path in BASH_SOURCE;
# POSIX sh does not, so fall back to the repo layout relative to the working
# directory, which is where every test and the Makefile run from.
if [ -n "${BASH_SOURCE[0]:-}" ]; then
    _mv_self=${BASH_SOURCE[0]}
else
    _mv_self=$0
fi
MV_PROFILE_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$_mv_self")" 2>/dev/null && pwd) || MV_PROFILE_SCRIPT_DIR=
if [ ! -f "$MV_PROFILE_SCRIPT_DIR/machine_profiles.py" ]; then
    MV_PROFILE_SCRIPT_DIR=$(CDPATH= cd -- tools 2>/dev/null && pwd) || MV_PROFILE_SCRIPT_DIR=.
fi
MV_MACHINE_PY=${MV_MACHINE_PY:-python3}
# A test that runs without the Makefile still gets the default machine.
MV_PROFILE=${MV_PROFILE:-p4}
export MV_PROFILE

mv_machine_args() {
    # One argument per line: no quoting rules to get wrong when a CPU model
    # string contains spaces and parentheses.
    MV_MACHINE_ARGS=()
    while IFS= read -r mv_line; do
        [ -n "$mv_line" ] && MV_MACHINE_ARGS+=("$mv_line")
    done <<EOF
$("$MV_MACHINE_PY" "$MV_PROFILE_SCRIPT_DIR/machine_profiles.py" --qemu-args)
EOF
}

mv_machine_flags() {
    "$MV_MACHINE_PY" "$MV_PROFILE_SCRIPT_DIR/machine_profiles.py" --qemu-flags
}

mv_machine_cpu() {
    "$MV_MACHINE_PY" "$MV_PROFILE_SCRIPT_DIR/machine_profiles.py" --cpu
}

mv_machine_mem() {
    "$MV_MACHINE_PY" "$MV_PROFILE_SCRIPT_DIR/machine_profiles.py" --mem
}

mv_machine_supported() {
    "$MV_MACHINE_PY" "$MV_PROFILE_SCRIPT_DIR/machine_profiles.py" --supported
}
