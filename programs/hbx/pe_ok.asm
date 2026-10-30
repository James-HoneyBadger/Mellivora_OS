; pe_ok.asm - the payload behind programs/pe_ok.bin, and behind every other PE32
; fixture as well.
;
; tools/pe_fixtures.py wraps this one payload into one image per rule the loader
; can refuse on (docs/spec/EXEC.md sections 1 and 8). It is assembled flat, not
; as a program: the wrapper supplies the DOS header, the COFF header, the
; optional header and the section table, and puts this code in one section at
; RVA 0 in an image based at PROGRAM_BASE, so it runs where it was assembled.
%include "syscalls.inc"
%include "pe_payload.inc"
