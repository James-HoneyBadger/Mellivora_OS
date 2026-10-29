; hbx_badsum.asm - the payload behind programs/hbx_badsum.bin.
;
; The image itself is wrapped by tools/hbx_fixtures.py, which is where the
; header differs for each of the loader's refusals (docs/spec/EXEC.md section 8).
%include "syscalls.inc"
%include "payload_ok.inc"
