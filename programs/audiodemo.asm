; audiodemo.asm  —  Audio capabilities demo for Mellivora OS
;
; Demonstrates:
;   SYS_AUDIO_STATUS  — query SB16 presence and playback state
;   SYS_PCI_FIND      — detect Intel AC97 audio hardware
;   SYS_BEEP          — PC speaker melody (always available)
;   SYS_AUDIO_PLAY    — 16-bit signed mono PCM via SB16 (if present)
;   SYS_MALLOC / SYS_FREE — runtime PCM buffer allocation
;
; Press any key after the melody to exit.

%include "syscalls.inc"

; PCM buffer parameters: 440 Hz square wave, 16-bit signed mono, 22050 Hz
PCM_SAMPLES  equ 22050          ; 1 second of audio
PCM_BYTES    equ PCM_SAMPLES * 2
PCM_RATE     equ 22050
PCM_FMT      equ PCM_RATE | (1 << 16) | (1 << 18)   ; 16-bit signed

; Note frequencies (Hz) for SYS_BEEP melody
NOTE_C4 equ 262
NOTE_E4 equ 330
NOTE_G4 equ 392
NOTE_C5 equ 523
NOTE_REST equ 0

start:
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_banner
        int 0x80

        ; ---- Detect SB16 ----
        mov eax, SYS_AUDIO_STATUS
        int 0x80
        ; EAX = state, EBX = sb16_present
        mov [aud_sb16], ebx

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_sb16_label
        int 0x80
        cmp dword [aud_sb16], 0
        je .sb16_absent
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_present
        int 0x80
        jmp .sb16_done
.sb16_absent:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_absent
        int 0x80
.sb16_done:

        ; ---- Detect AC97 via PCI ----
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_ac97_label
        int 0x80

        ; Try ICH5 AC97 (8086:2415) first, then ICH6 (8086:2445)
        mov eax, SYS_PCI_FIND
        mov ebx, 0x8086
        mov ecx, 0x2415
        int 0x80
        cmp eax, -1
        jne .ac97_found
        mov eax, SYS_PCI_FIND
        mov ebx, 0x8086
        mov ecx, 0x2445
        int 0x80
        cmp eax, -1
        jne .ac97_found
        mov eax, SYS_PCI_FIND
        mov ebx, 0x8086
        mov ecx, 0x2425
        int 0x80
        cmp eax, -1
        je .ac97_absent

.ac97_found:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_present
        int 0x80
        jmp .ac97_done
.ac97_absent:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_absent
        int 0x80
.ac97_done:

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_sep
        int 0x80

        ; ---- PC speaker melody (always works) ----
        mov eax, SYS_PRINT
        mov ebx, msg_beep_start
        int 0x80

        ; C major arpeggio: C4-E4-G4-C5
        mov eax, SYS_BEEP
        mov ebx, NOTE_C4
        mov ecx, 15
        int 0x80

        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80

        mov eax, SYS_BEEP
        mov ebx, NOTE_E4
        mov ecx, 15
        int 0x80

        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80

        mov eax, SYS_BEEP
        mov ebx, NOTE_G4
        mov ecx, 15
        int 0x80

        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80

        mov eax, SYS_BEEP
        mov ebx, NOTE_C5
        mov ecx, 25
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_beep_done
        int 0x80

        ; ---- SB16 PCM playback (if present) ----
        cmp dword [aud_sb16], 0
        je .skip_pcm

        mov eax, SYS_PRINT
        mov ebx, msg_pcm_gen
        int 0x80

        ; Allocate PCM buffer (22050 samples * 2 bytes = 44100 bytes)
        mov eax, SYS_MALLOC
        mov ebx, PCM_BYTES
        int 0x80
        test eax, eax
        jz .skip_pcm
        mov [aud_pcm_buf], eax

        ; Generate 440 Hz square wave into buffer
        ; Period = 50 samples (22050/440), half-period = 25
        mov edi, [aud_pcm_buf]
        mov ecx, PCM_SAMPLES
        xor edx, edx            ; phase counter
.gen_loop:
        cmp edx, 25
        jl  .gen_pos
        mov ax, 0x8001          ; -32767 (negative half-cycle)
        jmp .gen_store
.gen_pos:
        mov ax, 0x7FFF          ; +32767 (positive half-cycle)
.gen_store:
        stosw                   ; store word, advance EDI by 2
        inc edx
        cmp edx, 50
        jl  .gen_no_wrap
        xor edx, edx
.gen_no_wrap:
        loop .gen_loop

        mov eax, SYS_PRINT
        mov ebx, msg_pcm_play
        int 0x80

        ; Play the PCM buffer
        mov eax, SYS_AUDIO_PLAY
        mov ebx, [aud_pcm_buf]
        mov ecx, PCM_BYTES
        mov edx, PCM_FMT
        int 0x80

        ; Wait for playback to finish (poll state)
.wait_pcm:
        mov eax, SYS_SLEEP
        mov ebx, 10             ; 100 ms
        int 0x80
        mov eax, SYS_AUDIO_STATUS
        int 0x80
        cmp eax, 1              ; state 1 = playing
        je .wait_pcm

        mov eax, SYS_AUDIO_STOP
        int 0x80

        ; Free the buffer
        mov eax, SYS_FREE
        mov ebx, [aud_pcm_buf]
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_pcm_done
        int 0x80

.skip_pcm:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_prompt
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

; ---- Variables ----
aud_sb16:    dd 0
aud_pcm_buf: dd 0

; ---- Strings ----
msg_banner:
        db "===== Mellivora OS Audio Demo =====", 0x0A, 0x0A, 0
msg_sb16_label:
        db "SB16 sound card:  ", 0
msg_ac97_label:
        db "Intel AC97 audio: ", 0
msg_present:
        db "PRESENT", 0x0A, 0
msg_absent:
        db "absent", 0x0A, 0
msg_sep:
        db 0x0A, 0
msg_beep_start:
        db "Playing C-major arpeggio on PC speaker...", 0x0A, 0
msg_beep_done:
        db "Beep done.", 0x0A, 0
msg_pcm_gen:
        db "Generating 440 Hz PCM tone (1 second)...", 0x0A, 0
msg_pcm_play:
        db "Playing via SB16 DMA...", 0x0A, 0
msg_pcm_done:
        db "PCM playback complete.", 0x0A, 0
msg_prompt:
        db 0x0A, "Press any key to exit...", 0x0A, 0
