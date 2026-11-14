; rewrite_test.asm - write a file, read it back, and check every byte
;
; A file's bytes can be lost in the seam between two writes to the same name: the
; old blocks are freed, new ones are allocated, the entry is updated, and any of
; those steps that forgets what the caller asked for leaves an entry that points
; at the right place with the wrong content - or at nothing. Reading the bytes
; back after every round is the only way to notice, because every other check
; (the size, the directory entry, the free count) can look right while the
; content is not.
;
; Twelve rounds, alternating the two shapes a rewrite takes: five replace the
; file (delete, then create) and the rest overwrite it in place. Each round's
; payload carries its own round number, so a read that returns the previous
; round's bytes fails rather than passes.
;
; Run it with and without a snapshot taken: the snapshot's write hook copies a
; block before it changes, and the copy is written through the same code the
; caller's write uses.
;
; Prints REWRITE_OK, or REWRITE_FAIL round <n> with the round that disagreed.

%include "syscalls.inc"

ROUNDS          equ 12
PAYLOAD_LEN     equ 64

set_stage:
        mov [stage], al
        ret

fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, stage
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_round
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, round_str
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

start:
        mov dword [round], 0
        ; Every round is one more write sequence; a stale entry from a run that
        ; died half way must not be read as this run's work.
        mov eax, SYS_DELETE
        mov ebx, file_name
        int 0x80

.next_round:
        mov eax, [round]
        cmp eax, ROUNDS
        jae .done

        ; The payload names its round in the first byte and is filled with a
        ; character that changes with it, so a read that returns a neighbour's
        ; content differs in more than one place.
        mov al, 'A'
        add al, byte [round]
        mov edi, payload
        mov ecx, PAYLOAD_LEN
        cld
        rep stosb
        mov eax, [round]
        add al, '0'
        mov [payload], al
        mov byte [payload + 1], 10

        mov al, '1'
        call set_stage
        ; Odd rounds replace the file; even rounds overwrite it where it is.
        mov eax, [round]
        test eax, 1
        jnz .write
        mov eax, SYS_DELETE
        mov ebx, file_name
        int 0x80

.write:
        mov al, '2'
        call set_stage
        mov eax, SYS_FWRITE
        mov ebx, file_name
        mov ecx, payload
        mov edx, PAYLOAD_LEN
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne fail
        call show_round

        mov al, '3'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, file_name
        mov ecx, read_buf
        int 0x80
        cmp eax, PAYLOAD_LEN
        jne fail

        ; Byte for byte, not just the length.
        mov al, '4'
        call set_stage
        mov esi, payload
        mov edi, read_buf
        mov ecx, PAYLOAD_LEN
        cld
        repe cmpsb
        jne fail

        inc dword [round]
        mov eax, [round]
        add al, '0'
        mov [round_str], al
        jmp .next_round

.done:
        mov eax, SYS_DELETE
        mov ebx, file_name
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

show_round:
        mov eax, [round]
        add al, '0'
        mov [stage], al
        ret

file_name:  db "rw_probe.txt", 0
msg_ok:     db "REWRITE_OK", 10, 0
msg_fail:   db "REWRITE_FAIL stage ", 0
msg_round:  db " round ", 0
msg_newline: db 10, 0
stage:      db '0', 0
round_str:  db '0', 0
round:      dd 0
payload:    times PAYLOAD_LEN db 0
read_buf:   times PAYLOAD_LEN db 0
