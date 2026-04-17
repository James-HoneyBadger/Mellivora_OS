; rain.asm - Matrix digital rain animation in text mode
; Usage: rain
; Press any key to exit

%include "syscalls.inc"

NUM_COLS  equ 80
NUM_DROPS equ 40                ; Active rain drops
FRAME_DELAY equ 3               ; Ticks between frames (~30ms)

start:
        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Initialize drops
        call init_drops

.main_loop:
        ; Check for keypress
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .exit

        ; Update and render
        call update_drops
        call render_frame

        ; Delay
        mov eax, SYS_SLEEP
        mov ebx, FRAME_DELAY
        int 0x80

        jmp .main_loop

.exit:
        ; Restore colors and clear
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; init_drops - Initialize rain drop positions
; Each drop: col (byte), row (byte), speed (byte), length (byte), char (byte)
;---------------------------------------
DROP_SIZE equ 5

init_drops:
        mov edi, drops
        mov ecx, NUM_DROPS
        mov eax, 12345          ; Simple seed
.init_loop:
        ; Pseudo-random column (0-79)
        imul eax, 1103515245
        add eax, 12345
        push rax
        xor edx, edx
        mov ebx, NUM_COLS
        div ebx
        mov [edi], dl           ; col = rand % 80
        pop rax

        ; Random starting row (negative = off screen)
        imul eax, 1103515245
        add eax, 12345
        push rax
        and eax, 0x1F           ; 0-31
        neg al                  ; Start above screen
        mov [edi + 1], al       ; row
        pop rax

        ; Speed (1-3)
        imul eax, 1103515245
        add eax, 12345
        push rax
        and eax, 0x01
        inc al                  ; 1-2
        mov [edi + 2], al       ; speed
        pop rax

        ; Trail length (4-12)
        imul eax, 1103515245
        add eax, 12345
        push rax
        and eax, 0x07
        add al, 5               ; 5-12
        mov [edi + 3], al       ; length
        pop rax

        ; Random character
        imul eax, 1103515245
        add eax, 12345
        push rax
        and eax, 0x5E           ; wide range
        add al, 0x21            ; printable ASCII range
        mov [edi + 4], al
        pop rax

        add edi, DROP_SIZE
        dec ecx
        jnz .init_loop
        mov [rand_state], eax
        ret

;---------------------------------------
; update_drops - Move drops down, respawn at top
;---------------------------------------
update_drops:
        mov esi, drops
        mov ecx, NUM_DROPS
.upd_loop:
        ; Move down by speed
        movzx eax, byte [esi + 2]  ; speed
        add [esi + 1], al          ; row += speed

        ; Check if fully off screen (row - length > 24)
        movsx eax, byte [esi + 1]
        movzx edx, byte [esi + 3]  ; length
        sub eax, edx
        cmp eax, 25
        jl .no_respawn

        ; Respawn: new random column, row = -length
        mov eax, [rand_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_state], eax
        push rax
        xor edx, edx
        mov ebx, NUM_COLS
        div ebx
        mov [esi], dl           ; new column
        pop rax

        ; New row above screen
        movzx edx, byte [esi + 3]
        neg dl
        mov [esi + 1], dl

        ; New random char
        imul eax, 1103515245
        add eax, 12345
        mov [rand_state], eax
        and eax, 0x5E
        add al, 0x21
        mov [esi + 4], al

        ; New speed
        mov eax, [rand_state]
        and eax, 0x01
        inc al
        mov [esi + 2], al

.no_respawn:
        ; Occasionally mutate the drop's character
        mov eax, [rand_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_state], eax
        test eax, 0x07          ; 1 in 8 chance
        jnz .no_mutate
        and eax, 0x5E
        add al, 0x21
        mov [esi + 4], al
.no_mutate:

        add esi, DROP_SIZE
        dec ecx
        jnz .upd_loop
        ret

;---------------------------------------
; render_frame - Draw all drops directly to VGA
;---------------------------------------
render_frame:
        PUSHALL

        ; Clear VGA buffer to black
        mov edi, VGA_BASE
        mov ecx, 80 * 25
        mov ax, 0x0020          ; Black space
        rep stosw

        ; Draw each drop
        mov esi, drops
        mov ebp, NUM_DROPS
.draw_drop:
        movzx ebx, byte [esi]      ; col
        movsx edx, byte [esi + 1]  ; row (head)
        movzx ecx, byte [esi + 3]  ; length

.draw_trail:
        ; Draw positions from (row) to (row - length)
        push rcx
        push rdx

        ; Check if row is on screen
        cmp edx, 0
        jl .draw_skip
        cmp edx, 25
        jge .draw_skip

        ; Calculate VGA offset: (row * 80 + col) * 2
        mov eax, edx
        imul eax, 80
        add eax, ebx
        shl eax, 1

        ; Determine color based on distance from head
        xor edi, edi
        movzx edi, byte [esi + 3]
        sub edi, ecx            ; distance from head

        cmp edi, 0
        jne .not_head
        ; Head: bright white character
        mov byte [VGA_BASE + eax + 1], 0x0F
        movzx ecx, byte [esi + 4]
        mov [VGA_BASE + eax], cl
        jmp .draw_skip

.not_head:
        cmp edi, 1
        jg .not_bright
        ; Just behind head: bright green
        mov byte [VGA_BASE + eax + 1], 0x0A
        movzx ecx, byte [esi + 4]
        add cl, byte [esi]     ; Vary char by position
        cmp cl, 0x21
        jge .ch_ok1
        add cl, 0x30
.ch_ok1:
        mov [VGA_BASE + eax], cl
        jmp .draw_skip

.not_bright:
        cmp edi, 3
        jg .dim_trail
        ; Medium: green
        mov byte [VGA_BASE + eax + 1], 0x02
        movzx ecx, byte [esi + 4]
        sub cl, byte [esi]
        cmp cl, 0x21
        jge .ch_ok2
        add cl, 0x40
.ch_ok2:
        mov [VGA_BASE + eax], cl
        jmp .draw_skip

.dim_trail:
        ; Dim: dark green
        mov byte [VGA_BASE + eax + 1], 0x02
        mov byte [VGA_BASE + eax], 0xB0 ; Light shade

.draw_skip:
        pop rdx
        pop rcx
        dec edx                 ; Move up the trail
        dec ecx
        jnz .draw_trail

        add esi, DROP_SIZE
        dec ebp
        jnz .draw_drop

        POPALL
        ret

;=======================================================================
; DATA
;=======================================================================

rand_state: dd 0x5DEECE6D
drops:      times NUM_DROPS * DROP_SIZE db 0
