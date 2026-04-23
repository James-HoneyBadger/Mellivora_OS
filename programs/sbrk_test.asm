; =============================================================================
; sbrk_test.asm — Test program for the SYS_SBRK syscall
;
; Tests the following scenarios:
; 1. Get initial program break.
; 2. Allocate a small chunk of memory.
; 3. Write to the allocated memory and read it back.
; 4. Allocate a zero-byte chunk (should be a no-op).
; 5. Deallocate the memory.
; 6. Check that the break has returned to its original position.
; =============================================================================

bits 32
org 0x00200000

SYS_EXIT    equ 0
SYS_PRINT   equ 3
SYS_SBRK    equ 23

section .text
global _start

_start:
    ; --- Test 1: Get initial program break ---
    mov eax, SYS_PRINT
    mov ebx, msg_test1
    int 0x80

    mov eax, SYS_SBRK
    mov ebx, 0
    int 0x80
    mov [initial_brk], eax
    call print_hex      ; Print initial break address

    ; --- Test 2: Allocate 4096 bytes ---
    mov eax, SYS_PRINT
    mov ebx, msg_test2
    int 0x80

    mov eax, SYS_SBRK
    mov ebx, 4096
    int 0x80
    mov [new_brk], eax

    cmp eax, -1
    je .fail

    call print_hex      ; Print new break address (should be same as initial)

    ; --- Test 3: Write to and read from the new memory ---
    mov eax, SYS_PRINT
    mov ebx, msg_test3
    int 0x80

    mov edi, [new_brk]  ; Pointer to the new memory
    mov dword [edi], 0xDEADBEEF
    mov dword [edi + 4092], 0xCAFEBABE

    mov esi, [edi]
    mov edx, [edi + 4092]

    cmp esi, 0xDEADBEEF
    jne .fail
    cmp edx, 0xCAFEBABE
    jne .fail

    mov eax, SYS_PRINT
    mov ebx, msg_ok
    int 0x80

    ; --- Test 4: Allocate 0 bytes ---
    mov eax, SYS_PRINT
    mov ebx, msg_test4
    int 0x80

    mov eax, SYS_SBRK
    mov ebx, 0
    int 0x80
    call print_hex      ; Should be initial_brk + 4096

    ; --- Test 5: Deallocate 4096 bytes ---
    mov eax, SYS_PRINT
    mov ebx, msg_test5
    int 0x80

    mov eax, SYS_SBRK
    mov ebx, -4096
    int 0x80
    call print_hex      ; Should be initial_brk + 4096

    ; --- Test 6: Check final break ---
    mov eax, SYS_PRINT
    mov ebx, msg_test6
    int 0x80

    mov eax, SYS_SBRK
    mov ebx, 0
    int 0x80
    call print_hex      ; Should be back to initial_brk

    cmp eax, [initial_brk]
    jne .fail

    ; --- Success ---
    mov eax, SYS_PRINT
    mov ebx, msg_success
    int 0x80
    jmp .exit

.fail:
    mov eax, SYS_PRINT
    mov ebx, msg_fail
    int 0x80

.exit:
    mov eax, SYS_EXIT
    int 0x80

; --- Helper function to print EAX as 8-digit hex ---
print_hex:
    pushad
    mov edi, hex_buffer + 9
    mov byte [edi], 0
    mov ecx, 8
.hex_loop:
    dec edi
    mov edx, eax
    and edx, 0x0F
    cmp edx, 10
    jl .is_digit
    add dl, 'A' - 10
    jmp .store
.is_digit:
    add dl, '0'
.store:
    mov [edi], dl
    shr eax, 4
    loop .hex_loop

    mov eax, SYS_PRINT
    mov ebx, hex_buffer
    int 0x80
    popad
    ret

section .data
msg_test1:  db "1. Getting initial program break: ", 0
msg_test2:  db "2. Allocating 4096 bytes. Old break: ", 0
msg_test3:  db "3. Writing and reading from new memory: ", 0
msg_test4:  db "4. Getting current break (after 0-byte alloc): ", 0
msg_test5:  db "5. Deallocating 4096 bytes. Old break: ", 0
msg_test6:  db "6. Getting final program break: ", 0
msg_ok:     db "OK", 10, 0
msg_success:db 10, "All tests passed!", 10, 0
msg_fail:   db 10, "Test FAILED!", 10, 0
hex_buffer: db "0x", 0,0,0,0,0,0,0,0, 10, 0

section .bss
initial_brk: resd 1
new_brk:     resd 1
