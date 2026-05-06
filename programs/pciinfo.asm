; pciinfo.asm  —  PCI hardware inventory for Mellivora OS
;
; Demonstrates SYS_PCI_FIND (101): probe for known PCI devices by
; vendor:device ID and display their bus/device/function address.
;
; Press any key to exit.

%include "syscalls.inc"

PCI_DEV_COUNT equ 10        ; entries in probe table

start:
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; bright white
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_hdr
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; --- Main scan loop ---
        mov dword [pi_idx], 0

.scan:
        mov esi, [pi_idx]
        cmp esi, PCI_DEV_COUNT
        jge .done

        ; Load vendor:device IDs from table
        movzx ebx, word [pi_vendors + esi * 2]
        movzx ecx, word [pi_devids  + esi * 2]

        ; Call SYS_PCI_FIND -> EAX = bdf (bus<<16|dev<<8|fn) or -1
        mov eax, SYS_PCI_FIND
        int 0x80
        mov [pi_bdf], eax

        ; Print device name (padded to column width)
        mov edi, [pi_names + esi * 4]
        push esi
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        pop esi

        ; Print present/absent status
        cmp dword [pi_bdf], -1
        je .absent

        ; Found — print in green with BDF address
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; light green
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_found
        int 0x80

        ; Print bus byte
        mov eax, [pi_bdf]
        shr eax, 16
        and eax, 0xFF
        call print_hex8

        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80

        ; Print device byte
        mov eax, [pi_bdf]
        shr eax, 8
        and eax, 0xFF
        call print_hex8

        mov eax, SYS_PUTCHAR
        mov ebx, '.'
        int 0x80

        ; Print function byte
        mov eax, [pi_bdf]
        and eax, 0xFF
        call print_hex8

        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A           ; newline
        int 0x80
        jmp .next

.absent:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08           ; dark grey
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_absent
        int 0x80

.next:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        inc dword [pi_idx]
        jmp .scan

.done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_done
        int 0x80
        ; Block until keypress
        mov eax, SYS_GETCHAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; -----------------------------------------------------------------------
; print_hex8  —  print low byte of EAX as two uppercase hex digits
; Preserves: EAX, EBX, ECX
; -----------------------------------------------------------------------
print_hex8:
        push eax
        push ebx
        push ecx
        and eax, 0xFF
        shl eax, 24             ; put byte into bits 31..24
        mov ecx, 2
.ph8_loop:
        rol eax, 4              ; next nibble into bits 3..0
        push eax
        and eax, 0xF
        add al, '0'
        cmp al, '9'
        jle .ph8_ok
        add al, 'A' - '9' - 1  ; '0'+10 -> 'A', etc.
.ph8_ok:
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop eax
        loop .ph8_loop
        pop ecx
        pop ebx
        pop eax
        ret

; -----------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------
pi_idx:  dd 0
pi_bdf:  dd 0

msg_hdr:    db "===== PCI Hardware Inventory =====", 0x0A, 0x0A, 0
msg_found:  db "  FOUND @ ", 0
msg_absent: db "  absent", 0x0A, 0
msg_done:   db 0x0A, "Press any key to exit...", 0x0A, 0

; Vendor IDs (10 entries, one word each)
pi_vendors:
        dw 0x8086               ; Intel
        dw 0x8086               ; Intel
        dw 0x8086               ; Intel
        dw 0x8086               ; Intel
        dw 0x8086               ; Intel
        dw 0x8086               ; Intel
        dw 0x8086               ; Intel
        dw 0x10EC               ; Realtek
        dw 0x1AF4               ; VirtIO
        dw 0x1AF4               ; VirtIO

; Device IDs (10 entries, one word each)
pi_devids:
        dw 0x1237               ; i440FX host bridge
        dw 0x7000               ; PIIX ISA bridge
        dw 0x7010               ; PIIX3 IDE controller
        dw 0x7111               ; PIIX4 IDE + Bus Master
        dw 0x2921               ; ICH9  IDE controller
        dw 0x2415               ; ICH5  AC97 audio
        dw 0x2445               ; ICH6  AC97 audio
        dw 0x8139               ; RTL8139 network
        dw 0x1001               ; VirtIO block
        dw 0x1000               ; VirtIO network

; Pointers to name strings (10 dwords)
pi_names:
        dd pi_n0, pi_n1, pi_n2, pi_n3, pi_n4
        dd pi_n5, pi_n6, pi_n7, pi_n8, pi_n9

pi_n0: db "Intel i440FX Host Bridge     ", 0
pi_n1: db "Intel PIIX  ISA Bridge       ", 0
pi_n2: db "Intel PIIX3 IDE Controller   ", 0
pi_n3: db "Intel PIIX4 IDE (BusMaster)  ", 0
pi_n4: db "Intel ICH9  IDE Controller   ", 0
pi_n5: db "Intel ICH5  AC97 Audio       ", 0
pi_n6: db "Intel ICH6  AC97 Audio       ", 0
pi_n7: db "Realtek RTL8139 Network      ", 0
pi_n8: db "VirtIO Block Device          ", 0
pi_n9: db "VirtIO Network Device        ", 0
