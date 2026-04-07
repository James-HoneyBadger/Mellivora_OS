; piano.asm - Simple PC speaker piano for Mellivora OS
; Uses PC speaker beep syscall to play musical notes
%include "syscalls.inc"

start:
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_keys
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_piano
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_help
        int 0x80

.loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        cmp al, 27
        je .quit

        ; Map key to note frequency
        cmp al, 'a'
        je .note_c4
        cmp al, 'w'
        je .note_cs4
        cmp al, 's'
        je .note_d4
        cmp al, 'e'
        je .note_ds4
        cmp al, 'd'
        je .note_e4
        cmp al, 'f'
        je .note_f4
        cmp al, 't'
        je .note_fs4
        cmp al, 'g'
        je .note_g4
        cmp al, 'y'
        je .note_gs4
        cmp al, 'h'
        je .note_a4
        cmp al, 'u'
        je .note_as4
        cmp al, 'j'
        je .note_b4
        cmp al, 'k'
        je .note_c5
        cmp al, 'o'
        je .note_cs5
        cmp al, 'l'
        je .note_d5

        ; Demo song
        cmp al, '1'
        je .play_scale
        cmp al, '2'
        je .play_mary

        jmp .loop

.note_c4:
        mov ebx, 262
        jmp .play
.note_cs4:
        mov ebx, 277
        jmp .play
.note_d4:
        mov ebx, 294
        jmp .play
.note_ds4:
        mov ebx, 311
        jmp .play
.note_e4:
        mov ebx, 330
        jmp .play
.note_f4:
        mov ebx, 349
        jmp .play
.note_fs4:
        mov ebx, 370
        jmp .play
.note_g4:
        mov ebx, 392
        jmp .play
.note_gs4:
        mov ebx, 415
        jmp .play
.note_a4:
        mov ebx, 440
        jmp .play
.note_as4:
        mov ebx, 466
        jmp .play
.note_b4:
        mov ebx, 494
        jmp .play
.note_c5:
        mov ebx, 523
        jmp .play
.note_cs5:
        mov ebx, 554
        jmp .play
.note_d5:
        mov ebx, 587
        jmp .play

.play:
        mov eax, SYS_BEEP
        mov ecx, 20             ; duration ~200ms
        int 0x80
        jmp .loop

.play_scale:
        mov esi, scale_notes
        mov ecx, 8
        call play_sequence
        jmp .loop

.play_mary:
        mov esi, mary_notes
        mov ecx, 13
        call play_sequence
        jmp .loop

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;---------------------------------------
; Play a sequence of notes
; ESI = pointer to word array of frequencies
; ECX = number of notes
;---------------------------------------
play_sequence:
        pushad
.ps_loop:
        cmp ecx, 0
        je .ps_done
        movzx ebx, word [esi]
        cmp ebx, 0
        je .ps_rest
        mov eax, SYS_BEEP
        push ecx
        mov ecx, 25
        int 0x80
        pop ecx
        jmp .ps_next
.ps_rest:
        mov eax, SYS_SLEEP
        mov ebx, 15
        int 0x80
.ps_next:
        add esi, 2
        mov eax, SYS_SLEEP
        mov ebx, 5
        int 0x80
        dec ecx
        jmp .ps_loop
.ps_done:
        popad
        ret

;---------------------------------------
; Data
;---------------------------------------
msg_title: db "=== Piano ===", 0x0A, 0x0A, 0
msg_keys:  db "  Keys:  a s d f g h j k l  (white keys: C4-D5)", 0x0A, 0
msg_piano: db "  Sharps: w e   t y u   o   (black keys)", 0x0A, 0x0A, 0
msg_help:  db "  [1] Play scale  [2] Mary Had a Little Lamb  [q] Quit", 0x0A, 0

; C major scale: C D E F G A B C
scale_notes: dw 262, 294, 330, 349, 392, 440, 494, 523

; Mary Had a Little Lamb: E D C D E E E - D D D - E G G
mary_notes: dw 330, 294, 262, 294, 330, 330, 330, 0, 294, 294, 294, 0, 330
