; bpaint.asm - Burrows Pixel Workshop
; Native indexed pixel art, bounded storage, keyboard and mouse editing.
; No host services, no external assets. One-level undo per edit/clear.
%include "syscalls.inc"
%include "lib/gui.inc"

W equ 32
H equ 24
CELL equ 10
X0 equ 8
Y0 equ 28
PY equ 280
WIN_W equ 360
WIN_H equ 336
N equ W*H

start:
    cld
    mov eax, 32
    mov ebx, 36
    mov ecx, WIN_W
    mov edx, WIN_H
    mov esi, title
    call gui_create_window
    cmp eax, -1
    je .fail
    mov [win], eax
    call redraw
.loop:
    call gui_poll_event
    cmp eax, EVT_CLOSE
    je .close
    cmp eax, EVT_KEY_PRESS
    je .key
    cmp eax, EVT_MOUSE_CLICK
    je .click
.idle:
    mov eax, SYS_SLEEP
    mov ebx, 2
    int 0x80
    jmp .loop
.key:
    cmp ebx, 27
    je .close
    cmp ebx, KEY_LEFT
    je .left
    cmp ebx, KEY_RIGHT
    je .right
    cmp ebx, KEY_UP
    je .up
    cmp ebx, KEY_DOWN
    je .down
    cmp ebx, ' '
    je .paint
    cmp ebx, 'u'
    je .undo
    cmp ebx, 'c'
    je .clear
    cmp ebx, '0'
    jb .idle
    cmp ebx, '9'
    ja .idle
    sub ebx, '0'
    mov [ink], bl
    jmp .refresh
.left:
    cmp dword [cursor_x], 0
    je .idle
    dec dword [cursor_x]
    jmp .refresh
.right:
    cmp dword [cursor_x], W-1
    jae .idle
    inc dword [cursor_x]
    jmp .refresh
.up:
    cmp dword [cursor_y], 0
    je .idle
    dec dword [cursor_y]
    jmp .refresh
.down:
    cmp dword [cursor_y], H-1
    jae .idle
    inc dword [cursor_y]
    jmp .refresh
.click:
    ; Only react to a pressed left button, not its release.
    push ebx
    push ecx
    mov eax, SYS_MOUSE
    int 0x80
    test ecx, 1
    pop ecx
    pop ebx
    jz .idle
    cmp ecx, PY
    jb .canvas
    cmp ecx, PY+20
    jae .idle
    sub ebx, X0
    cmp ebx, 10*30
    jae .idle
    mov eax, ebx
    xor edx, edx
    mov ecx, 30
    div ecx
    mov [ink], al
    jmp .refresh
.canvas:
    sub ebx, X0
    sub ecx, Y0
    cmp ebx, W*CELL
    jae .idle
    cmp ecx, H*CELL
    jae .idle
    mov eax, ebx
    xor edx, edx
    mov ebx, CELL
    div ebx
    mov [cursor_x], eax
    mov eax, ecx
    xor edx, edx
    div ebx
    mov [cursor_y], eax
.paint:
    call snapshot
    mov eax, [cursor_y]
    imul eax, W
    add eax, [cursor_x]
    mov dl, [ink]
    mov [pixels+eax], dl
    jmp .refresh
.clear:
    call snapshot
    cld
    mov edi, pixels
    xor eax, eax
    mov ecx, N
    rep stosb
    jmp .refresh
.undo:
    cmp byte [canundo], 0
    je .idle
    cld
    mov esi, previous
    mov edi, pixels
    mov ecx, N
    rep movsb
    mov byte [canundo], 0
.refresh:
    call redraw
    jmp .idle
.close:
    mov eax, [win]
    call gui_destroy_window
    xor ebx, ebx
    jmp .exit
.fail:
    mov ebx, 1
.exit:
    mov eax, SYS_EXIT
    int 0x80

snapshot:
    cld
    mov esi, pixels
    mov edi, previous
    mov ecx, N
    rep movsb
    mov byte [canundo], 1
    ret

redraw:
    pushad
    mov eax, [win]
    xor ebx, ebx
    xor ecx, ecx
    mov edx, WIN_W
    mov esi, WIN_H
    mov edi, 0x00304040
    call gui_fill_rect
    mov eax, [win]
    mov ebx, X0
    mov ecx, 6
    mov esi, heading
    mov edi, 0x00f5e8cb
    call gui_draw_text
    xor ebp, ebp
.row:
    xor eax, eax
    mov [col], eax
.cell:
    mov eax, ebp
    imul eax, W
    add eax, [col]
    movzx edi, byte [pixels+eax]
    mov edi, [palette+edi*4]
    mov eax, [win]
    imul ebx, [col], CELL
    add ebx, X0
    imul ecx, ebp, CELL
    add ecx, Y0
    mov edx, CELL-1
    mov esi, CELL-1
    call gui_fill_rect
    inc dword [col]
    cmp dword [col], W
    jb .cell
    inc ebp
    cmp ebp, H
    jb .row
    ; Cursor marker uses the grid border rather than altering pixel data.
    mov eax, [win]
    imul ebx, [cursor_x], CELL
    add ebx, X0
    imul ecx, [cursor_y], CELL
    add ecx, Y0+CELL-1
    mov edx, CELL
    mov esi, 1
    mov edi, 0x00ffffff
    call gui_fill_rect
    xor ebp, ebp
.swatch:
    mov eax, [win]
    imul ebx, ebp, 30
    add ebx, X0
    mov ecx, PY
    mov edx, 28
    mov esi, 20
    mov edi, 0x00607070
    call gui_fill_rect
    mov eax, [win]
    imul ebx, ebp, 30
    add ebx, X0+2
    mov ecx, PY+2
    mov edx, 24
    mov esi, 16
    mov edi, [palette+ebp*4]
    call gui_fill_rect
    inc ebp
    cmp ebp, 10
    jb .swatch
    mov eax, [win]
    movzx ebx, byte [ink]
    imul ebx, 30
    add ebx, X0
    mov ecx, PY+20
    mov edx, 28
    mov esi, 2
    mov edi, 0x00ffd080
    call gui_fill_rect
    mov eax, [win]
    mov ebx, X0
    mov ecx, 308
    mov esi, hint
    mov edi, 0x00f5e8cb
    call gui_draw_text
    call gui_compose
    call gui_flip
    popad
    ret

title db "Burrows Pixel Workshop",0
heading db "32 x 24   |   Indexed pixel canvas",0
hint db "Arrows Space  0-9 Ink  U Undo  C Clear",0
palette dd 0x00182028,0x00f5e8cb,0x00c06848,0x0048a080,0x005878b8
        dd 0x00e0b850,0x008868a8,0x0048b8c8,0x00889098,0x00ffffff
win dd 0
cursor_x dd 0
cursor_y dd 0
col dd 0
ink db 1
canundo db 0
pixels times N db 0
previous times N db 0
