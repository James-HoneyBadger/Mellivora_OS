; notify_demo.asm - Demonstrates SYS_NOTIFY and SYS_CLIPBOARD_COPY/PASTE
;
; Shows how to:
;   - Post system notifications (57 SYS_NOTIFY)
;   - Copy text to the clipboard (55 SYS_CLIPBOARD_COPY)
;   - Paste text from the clipboard (56 SYS_CLIPBOARD_PASTE)
;
; Good first program for learning the vNext service APIs.
; Press any key to cycle through demos, Q to quit.
%include "syscalls.inc"

CLIP_BUF_SIZE   equ 256
COLOR_TITLE     equ 0x0F        ; bright white
COLOR_DIM       equ 0x07        ; grey
COLOR_GOOD      equ 0x0A        ; light green
COLOR_INFO      equ 0x0B        ; light cyan
COLOR_KEY       equ 0x0E        ; yellow
NOTIF_CYAN      equ 0x00FFFF00  ; ARGB notification colour (cyan accent)
NOTIF_GREEN     equ 0x0000FF00  ; ARGB green
NOTIF_YELLOW    equ 0x00FFFF00  ; ARGB yellow

start:
        call print_header

        ; -- Step 1: copy a string to the clipboard --
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_INFO
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_step1
        int 0x80

        mov eax, SYS_CLIPBOARD_COPY
        mov ebx, clip_text
        mov ecx, clip_text_len
        int 0x80

        mov eax, SYS_NOTIFY
        mov ebx, notif_copied
        mov edx, NOTIF_CYAN
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_GOOD
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_copied
        int 0x80

        call wait_key

        ; -- Step 2: paste and display --
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_INFO
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_step2
        int 0x80

        mov eax, SYS_CLIPBOARD_PASTE
        mov ebx, paste_buf
        mov ecx, CLIP_BUF_SIZE
        int 0x80

        ; Null-terminate whatever we received
        cmp eax, 0
        jle .empty_paste
        cmp eax, CLIP_BUF_SIZE - 1
        jge .cap_paste
        mov byte [paste_buf + eax], 0
        jmp .show_paste
.cap_paste:
        mov byte [paste_buf + CLIP_BUF_SIZE - 1], 0
.show_paste:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_GOOD
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_pasted_prefix
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, paste_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        jmp .paste_done
.empty_paste:
        mov eax, SYS_PRINT
        mov ebx, msg_empty_paste
        int 0x80
.paste_done:

        call wait_key

        ; -- Step 3: post three notification styles --
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_INFO
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_step3
        int 0x80

        mov eax, SYS_NOTIFY
        mov ebx, notif_info
        mov edx, NOTIF_CYAN
        int 0x80
        mov eax, SYS_SLEEP
        mov ebx, 80
        int 0x80

        mov eax, SYS_NOTIFY
        mov ebx, notif_success
        mov edx, NOTIF_GREEN
        int 0x80
        mov eax, SYS_SLEEP
        mov ebx, 80
        int 0x80

        mov eax, SYS_NOTIFY
        mov ebx, notif_warning
        mov edx, NOTIF_YELLOW
        int 0x80
        mov eax, SYS_SLEEP
        mov ebx, 80
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_GOOD
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_done
        int 0x80

        call wait_key

.exit:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DIM
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;-------------------------------------------
print_header:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_TITLE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DIM
        int 0x80
        ret

;-------------------------------------------
wait_key:
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_KEY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_press_key
        int 0x80
.wk_spin:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .wk_spin
        cmp eax, 'q'
        je .wk_quit
        cmp eax, 'Q'
        je .wk_quit
        ret
.wk_quit:
        jmp start.exit

; ---------------------------------------------------------------------------
msg_title:          db "=== Notify & Clipboard Demo ===", 0x0A, 0
msg_step1:          db 0x0A, "Step 1: Copying text to clipboard...", 0x0A, 0
msg_copied:         db "  Done! Text copied.", 0x0A, 0
msg_step2:          db 0x0A, "Step 2: Pasting from clipboard...", 0x0A, 0
msg_pasted_prefix:  db "  Pasted: ", 0
msg_empty_paste:    db "  (clipboard was empty)", 0x0A, 0
msg_step3:          db 0x0A, "Step 3: Posting system notifications...", 0x0A, 0
msg_done:           db "  Three notifications posted!", 0x0A, 0
msg_press_key:      db "  [Press any key or Q to quit] ", 0
msg_newline:        db 0x0A, 0

notif_copied:       db "Clipboard updated", 0
notif_info:         db "Info: system notification example", 0
notif_success:      db "Success: operation complete", 0
notif_warning:      db "Warning: example warning message", 0

clip_text:          db "Hello from Mellivora clipboard!", 0
clip_text_len       equ $ - clip_text - 1

paste_buf:          times CLIP_BUF_SIZE db 0
