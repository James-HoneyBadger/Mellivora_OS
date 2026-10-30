; pe_import.asm - the payload behind programs/pe_import.bin.
;
; The image itself is wrapped by tools/pe_fixtures.py out of one payload
; (.build/hbx/pe.bin), which is where the header differs for each of the
; loader's refusals (docs/spec/EXEC.md sections 1 and 8). This file exists so
; populate.py can say what the binary came from, the same way the HBX fixture
; stubs do. The fixture is an image with an import directory, which has no libraries to resolve against.
%include "syscalls.inc"
%include "pe_payload.inc"
