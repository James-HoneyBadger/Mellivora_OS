; soundviz.asm — SB16 audio + VBE graphics demo for Mellivora OS
;
; Plays a three-note chord sequence (C4, E4, G4) on SB16 while drawing
; an animated waveform visualizer on the 640x480 framebuffer.
; Each note triggers a coloured expanding ring on screen.
; Falls back to PC-speaker beeps if SB16 is absent.
; Press any key to exit.

%include "syscalls.inc"

; ---- Screen constants ----
SCR_W   equ 640
SCR_H   equ 480
PITCH   equ SCR_W * 4
MID_X   equ 320
MID_Y   equ 240

; ---- Audio constants ----
; 22050 Hz, 16-bit signed mono
PCM_RATE    equ 22050
PCM_FMT     equ 22050 | (1 << 16) | (1 << 18)  ; rate | 16BIT | SIGNED
; 0.25 s per note = 5512 samples = 11024 bytes
NOTE_SAMP   equ 5512
NOTE_BYTES  equ 11024

; Note frequencies (Hz)
NOTE_C4     equ 262
NOTE_E4     equ 330
NOTE_G4     equ 392

; Ring colours for each note
COL_C   equ 0x00FF4040         ; red
COL_E   equ 0x0040FF40         ; green
COL_G   equ 0x004080FF         ; blue

; Max ring radius before it fades out
MAX_R   equ 200

; ---- Entry ----
start:
        ; -- Check SB16 present --
        mov eax, SYS_AUDIO_STATUS
        int 0x80
        mov [sv_sb16], ebx      ; 1 = present

        ; -- Set VBE mode 640x480x32 --
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je  .novbe

        ; -- Get shadow buffer --
        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [sv_shadow], eax

        ; -- Allocate PCM buffer (NOTE_BYTES bytes) via SYS_MALLOC --
        mov eax, SYS_MALLOC
        mov ebx, NOTE_BYTES
        int 0x80
        test eax, eax
        jz  .no_pcm
        mov [sv_pcm], eax
        jmp .main_loop

.no_pcm:
        mov dword [sv_sb16], 0  ; no buffer → treat as no SB16

.main_loop:
        ; Sequence: C4 → E4 → G4 → C4 → …  (3 notes, repeat)
        ; sv_note: 0, 1, 2 cycling

        ; --- Pick note frequency & ring colour ---
        mov eax, [sv_note]
        cmp eax, 0
        je  .do_c
        cmp eax, 1
        je  .do_e
        ; else G
.do_g:  mov dword [sv_freq],  NOTE_G4
        mov dword [sv_color], COL_G
        jmp .note_ready
.do_c:  mov dword [sv_freq],  NOTE_C4
        mov dword [sv_color], COL_C
        jmp .note_ready
.do_e:  mov dword [sv_freq],  NOTE_E4
        mov dword [sv_color], COL_E

.note_ready:
        ; --- Generate PCM square wave for this note into sv_pcm ---
        cmp dword [sv_sb16], 0
        je  .play_beep

        ; Generate: NOTE_SAMP 16-bit signed samples, square wave at sv_freq
        ; period_samples = PCM_RATE / freq
        ; half = period / 2
        mov eax, PCM_RATE
        xor edx, edx
        div dword [sv_freq]     ; eax = period in samples
        mov [sv_period], eax
        shr eax, 1
        mov [sv_half], eax

        mov edi, [sv_pcm]
        mov ecx, NOTE_SAMP
        xor ebx, ebx            ; sample counter within period
.gen_loop:
        cmp ebx, [sv_half]
        jl  .gen_hi
        ; low half: -8000
        mov word [edi], 0xE070  ; -8080 signed (≈ -8000)
        jmp .gen_next
.gen_hi:
        mov word [edi], 0x1F90  ; +8080 signed (≈ +8000)
.gen_next:
        add edi, 2
        inc ebx
        cmp ebx, [sv_period]
        jl  .gen_nomod
        xor ebx, ebx
.gen_nomod:
        dec ecx
        jnz .gen_loop

        ; --- Play PCM asynchronously ---
        mov eax, SYS_AUDIO_PLAY
        mov ebx, [sv_pcm]
        mov ecx, NOTE_BYTES
        mov edx, PCM_FMT
        int 0x80

        jmp .anim_start

.play_beep:
        ; SB16 absent — beep on PC speaker (non-blocking: use short duration)
        mov eax, SYS_BEEP
        mov ebx, [sv_freq]
        mov ecx, 5              ; 50 ms
        int 0x80

.anim_start:
        ; --- Animate: draw expanding ring over NOTE_SAMP/fps frames ---
        ; We'll run ~25 frames during the ~250 ms note (one SYS_SLEEP per frame)
        mov dword [sv_radius],  20
        mov dword [sv_frame],   0

.anim_frame:
        ; Key check (non-blocking)
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .done

        ; ---- Clear shadow ----
        mov edi, [sv_shadow]
        mov ecx, SCR_W * SCR_H
        mov eax, 0x00080C14     ; very dark blue-black
        rep stosd

        ; ---- Draw title ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 8
        mov edx, 8
        mov esi, sv_title
        mov edi, 0x00AAAAAA
        int 0x80

        ; ---- Draw note label ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 8
        mov edx, 24
        mov eax, [sv_note]
        cmp eax, 0
        je  .lbl_c
        cmp eax, 1
        je  .lbl_e
        mov esi, sv_lbl_g
        jmp .lbl_ok
.lbl_c: mov esi, sv_lbl_c
        jmp .lbl_ok
.lbl_e: mov esi, sv_lbl_e
.lbl_ok:
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov edx, 24
        mov ecx, 8
        mov edi, [sv_color]
        int 0x80

        ; ---- Draw ring: circle of pixels at radius sv_radius ----
        ; Use midpoint circle / Bresenham to draw a ring.
        ; For speed, draw 8-way symmetry points directly into shadow.
        call draw_circle

        ; ---- Present ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        ; ---- Advance radius ----
        add dword [sv_radius], 8
        cmp dword [sv_radius], MAX_R
        jle .anim_more
        mov dword [sv_radius], 20
.anim_more:

        ; ---- Delay ~10 ms ----
        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80

        inc dword [sv_frame]
        cmp dword [sv_frame], 25
        jl  .anim_frame

        ; --- Advance note ---
        inc dword [sv_note]
        cmp dword [sv_note], 3
        jl  .main_loop
        mov dword [sv_note], 0
        jmp .main_loop

.done:
        ; Stop any playing audio
        mov eax, SYS_AUDIO_STOP
        int 0x80

        ; Restore text mode
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80

.novbe:
        mov eax, SYS_PRINT
        mov ebx, sv_msg_exit
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; draw_circle — Bresenham midpoint circle, 8-way symmetry
; Draws a 1-pixel-wide ring at (MID_X, MID_Y) with radius [sv_radius]
; colour = [sv_color], written directly to [sv_shadow]
;=======================================================================
draw_circle:
        pushad
        ; r = sv_radius
        mov esi, [sv_radius]
        xor edi, edi            ; x = 0
        mov ebp, esi            ; y = r
        ; d = 1 - r  (decision variable)
        mov ecx, 1
        sub ecx, esi            ; ecx = d

.dc_loop:
        cmp edi, ebp
        jg  .dc_done

        ; Plot 8 symmetric points: (cx±x, cy±y) and (cx±y, cy±x)
        ; EAX/EBX scratch — each call: plot_px(MID_X+x, MID_Y+y), etc.
        push ecx
        push edi
        push ebp

        ; (+x, +y)
        mov eax, MID_X
        add eax, edi
        mov ebx, MID_Y
        add ebx, ebp
        call .plot

        ; (-x, +y)
        mov eax, MID_X
        sub eax, edi
        mov ebx, MID_Y
        add ebx, ebp
        call .plot

        ; (+x, -y)
        mov eax, MID_X
        add eax, edi
        mov ebx, MID_Y
        sub ebx, ebp
        call .plot

        ; (-x, -y)
        mov eax, MID_X
        sub eax, edi
        mov ebx, MID_Y
        sub ebx, ebp
        call .plot

        ; (+y, +x)
        mov eax, MID_X
        add eax, ebp
        mov ebx, MID_Y
        add ebx, edi
        call .plot

        ; (-y, +x)
        mov eax, MID_X
        sub eax, ebp
        mov ebx, MID_Y
        add ebx, edi
        call .plot

        ; (+y, -x)
        mov eax, MID_X
        add eax, ebp
        mov ebx, MID_Y
        sub ebx, edi
        call .plot

        ; (-y, -x)
        mov eax, MID_X
        sub eax, ebp
        mov ebx, MID_Y
        sub ebx, edi
        call .plot

        pop ebp
        pop edi
        pop ecx

        ; Update decision variable
        cmp ecx, 0
        jl  .dc_neg
        ; d >= 0: d += 2*(x-y)+5, y--
        mov eax, edi
        sub eax, ebp
        shl eax, 1
        add eax, 5
        add ecx, eax
        dec ebp
        jmp .dc_next
.dc_neg:
        ; d < 0: d += 2*x+3
        mov eax, edi
        shl eax, 1
        add eax, 3
        add ecx, eax
.dc_next:
        inc edi
        jmp .dc_loop

.dc_done:
        popad
        ret

; plot pixel at (EAX=x, EBX=y) with colour [sv_color] into [sv_shadow]
; Clips to screen. Preserves all regs.
.plot:
        cmp eax, 0
        jl  .pl_skip
        cmp eax, SCR_W
        jge .pl_skip
        cmp ebx, 0
        jl  .pl_skip
        cmp ebx, SCR_H
        jge .pl_skip
        push eax
        push ebx
        push ecx
        ; offset = y * PITCH + x * 4
        imul ebx, PITCH
        shl  eax, 2
        add  ebx, eax
        add  ebx, [sv_shadow]
        mov  ecx, [sv_color]
        mov  [ebx], ecx
        pop  ecx
        pop  ebx
        pop  eax
.pl_skip:
        ret

;=======================================================================
; Variables
;=======================================================================
sv_shadow:  dd 0
sv_pcm:     dd 0
sv_sb16:    dd 0
sv_note:    dd 0
sv_freq:    dd NOTE_C4
sv_color:   dd COL_C
sv_radius:  dd 20
sv_frame:   dd 0
sv_period:  dd 0
sv_half:    dd 0

sv_title:   db "soundviz - SB16 + VBE demo  (any key to exit)", 0
sv_lbl_c:   db "Note: C4 (262 Hz)", 0
sv_lbl_e:   db "Note: E4 (330 Hz)", 0
sv_lbl_g:   db "Note: G4 (392 Hz)", 0
sv_msg_exit: db "soundviz: done.", 0x0A, 0
