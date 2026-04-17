; bplayer.asm - WAV Audio Player for Mellivora OS (GUI)
; Usage: bplayer [filename.wav]
;
; GUI audio player with play/pause/stop controls and VU meter.
; Supports uncompressed PCM WAV files (8/16-bit, mono/stereo).

%include "syscalls.inc"
%include "lib/gui.inc"

WIN_W           equ 300
WIN_H           equ 200
COL_BG          equ 0x00303040
COL_PANEL       equ 0x00252535
COL_TEXT        equ 0x00DDDDDD
COL_ACCENT      equ 0x003388DD
COL_PLAY_BTN    equ 0x0033AA55
COL_STOP_BTN    equ 0x00CC3333
COL_PAUSE_BTN   equ 0x00CCAA33
COL_VU_BG       equ 0x00181828
COL_VU_BAR      equ 0x0044CC66
COL_VU_HIGH     equ 0x00CC4444
COL_PROGRESS    equ 0x003366CC
COL_PROG_BG     equ 0x00222233

FILE_BUF_SIZE   equ 512000      ; 500 KB max WAV
VU_BARS         equ 16
VU_BAR_W        equ 14
VU_BAR_H        equ 60
VU_X            equ 25
VU_Y            equ 50

; Audio states
STATE_STOPPED   equ 0
STATE_PLAYING   equ 1
STATE_PAUSED    equ 2

start:
        ; Get arguments
        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80

        ; Create window
        mov eax, 170
        mov ebx, 140
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, title_str
        call gui_create_window
        mov [win_id], eax

        ; If filename given, load it
        cmp byte [arg_buf], 0
        je main_loop
        call load_wav

main_loop:
        call draw_player
        call gui_compose
        call gui_flip

        ; Poll events
        call gui_poll_event
        cmp eax, EVT_CLOSE
        je exit_app
        cmp eax, EVT_KEY_PRESS
        je handle_key
        cmp eax, EVT_MOUSE_CLICK
        je handle_click

        ; Update VU meter if playing
        cmp byte [play_state], STATE_PLAYING
        jne main_loop

        ; Simple VU decay
        xor ecx, ecx
.vu_decay:
        cmp ecx, VU_BARS
        jge main_loop
        cmp byte [vu_levels + ecx], 0
        je .vu_next
        dec byte [vu_levels + ecx]
.vu_next:
        inc ecx
        jmp .vu_decay

;--- Draw the player interface ---
draw_player:
        PUSHALL

        ; Background
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, WIN_H
        mov edi, COL_BG
        call gui_fill_rect

        ; Title / filename
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, 8
        cmp byte [loaded], 0
        je .draw_no_file
        mov esi, filename
        jmp .draw_title
.draw_no_file:
        mov esi, str_no_file
.draw_title:
        mov edi, COL_TEXT
        call gui_draw_text

        ; File info line
        cmp byte [loaded], 0
        je .draw_no_info
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, 24
        mov esi, info_line
        mov edi, 0x00888899
        call gui_draw_text
.draw_no_info:

        ; VU meter background
        mov eax, [win_id]
        mov ebx, VU_X - 2
        mov ecx, VU_Y - 2
        mov edx, VU_BARS * (VU_BAR_W + 2) + 4
        mov esi, VU_BAR_H + 4
        mov edi, COL_VU_BG
        call gui_fill_rect

        ; VU bars
        xor ecx, ecx    ; bar index
.draw_vu:
        cmp ecx, VU_BARS
        jge .draw_controls
        push rcx

        ; Bar position
        mov ebx, ecx
        imul ebx, (VU_BAR_W + 2)
        add ebx, VU_X

        ; Bar height from level
        movzx edx, byte [vu_levels + ecx]
        cmp edx, VU_BAR_H
        jbe .vu_clamp_ok
        mov edx, VU_BAR_H
.vu_clamp_ok:

        ; Draw the bar (from bottom up)
        test edx, edx
        jz .vu_bar_done

        mov eax, [win_id]
        push rdx
        push rbx
        mov ecx, VU_Y + VU_BAR_H
        sub ecx, edx           ; top of bar
        mov esi, edx           ; height = level
        mov edx, VU_BAR_W
        ; Color based on level
        pop rbx
        pop rdi                 ; edi = level
        push rdi
        push rbx
        cmp edi, 50
        jg .vu_red
        mov edi, COL_VU_BAR
        jmp .vu_draw
.vu_red:
        mov edi, COL_VU_HIGH
.vu_draw:
        ; eax=win, ebx=x, ecx=y, edx=w, esi=h, edi=color
        ; Need to re-set esi to height
        pop rbx
        pop rsi                ; esi = level (height)
        push rsi
        push rbx
        call gui_fill_rect
        pop rbx
        pop rdx
        jmp .vu_next_bar

.vu_bar_done:
        pop rcx
        inc ecx
        jmp .draw_vu

.vu_next_bar:
        pop rcx
        inc ecx
        jmp .draw_vu

.draw_controls:
        ; Progress bar background
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, VU_Y + VU_BAR_H + 10
        mov edx, WIN_W - 20
        mov esi, 8
        mov edi, COL_PROG_BG
        call gui_fill_rect

        ; Progress bar fill
        cmp byte [loaded], 0
        je .draw_buttons
        cmp dword [pcm_total_len], 0
        je .draw_buttons

        ; Calculate progress width
        mov eax, [pcm_played]
        mov ecx, WIN_W - 20
        mul ecx
        div dword [pcm_total_len]     ; eax = progress pixels
        test eax, eax
        jz .draw_buttons

        push rax
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, VU_Y + VU_BAR_H + 10
        pop rdx
        mov esi, 8
        mov edi, COL_PROGRESS
        call gui_fill_rect

.draw_buttons:
        ; Play button
        mov eax, [win_id]
        mov ebx, 50
        mov ecx, VU_Y + VU_BAR_H + 28
        mov edx, 50
        mov esi, 28
        mov edi, COL_PLAY_BTN
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 60
        mov ecx, VU_Y + VU_BAR_H + 34
        mov esi, str_play
        mov edi, 0x00FFFFFF
        call gui_draw_text

        ; Pause button
        mov eax, [win_id]
        mov ebx, 110
        mov ecx, VU_Y + VU_BAR_H + 28
        mov edx, 50
        mov esi, 28
        mov edi, COL_PAUSE_BTN
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 117
        mov ecx, VU_Y + VU_BAR_H + 34
        mov esi, str_pause
        mov edi, 0x00FFFFFF
        call gui_draw_text

        ; Stop button
        mov eax, [win_id]
        mov ebx, 170
        mov ecx, VU_Y + VU_BAR_H + 28
        mov edx, 50
        mov esi, 28
        mov edi, COL_STOP_BTN
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 180
        mov ecx, VU_Y + VU_BAR_H + 34
        mov esi, str_stop
        mov edi, 0x00FFFFFF
        call gui_draw_text

        ; Status text
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, WIN_H - 22
        mov esi, str_status_stopped
        cmp byte [play_state], STATE_STOPPED
        je .draw_status
        mov esi, str_status_playing
        cmp byte [play_state], STATE_PLAYING
        je .draw_status
        mov esi, str_status_paused
.draw_status:
        mov edi, COL_ACCENT
        call gui_draw_text

        POPALL
        ret

;--- Handle keyboard ---
handle_key:
        ; EBX = key code
        cmp ebx, ' '
        je toggle_play
        cmp ebx, 's'
        je stop_playback
        cmp ebx, 'q'
        je exit_app
        cmp ebx, 0x1B
        je exit_app
        jmp main_loop

;--- Handle mouse click ---
handle_click:
        ; EBX = x, ECX = y (window-relative)
        ; Play button: (50, VU_Y+VU_BAR_H+28) to (100, VU_Y+VU_BAR_H+56)
        mov eax, VU_Y + VU_BAR_H + 28
        cmp ecx, eax
        jl main_loop
        mov eax, VU_Y + VU_BAR_H + 56
        cmp ecx, eax
        jg main_loop

        cmp ebx, 50
        jl main_loop
        cmp ebx, 100
        jle do_play
        cmp ebx, 110
        jl main_loop
        cmp ebx, 160
        jle toggle_pause
        cmp ebx, 170
        jl main_loop
        cmp ebx, 220
        jle stop_playback
        jmp main_loop

do_play:
        cmp byte [loaded], 0
        je main_loop
        ; Start playback from beginning
        mov dword [pcm_played], 0
        call start_audio
        jmp main_loop

toggle_play:
        cmp byte [loaded], 0
        je main_loop
        cmp byte [play_state], STATE_PLAYING
        je toggle_pause
        cmp byte [play_state], STATE_PAUSED
        je resume_audio
        ; Start from beginning
        mov dword [pcm_played], 0
        call start_audio
        jmp main_loop

toggle_pause:
        cmp byte [play_state], STATE_PLAYING
        jne resume_audio
        ; Pause
        mov eax, SYS_AUDIO_STOP
        int 0x80
        mov byte [play_state], STATE_PAUSED
        jmp main_loop

resume_audio:
        cmp byte [play_state], STATE_PAUSED
        jne main_loop
        call start_audio
        jmp main_loop

stop_playback:
        mov eax, SYS_AUDIO_STOP
        int 0x80
        mov byte [play_state], STATE_STOPPED
        mov dword [pcm_played], 0
        ; Clear VU
        mov edi, vu_levels
        xor eax, eax
        mov ecx, VU_BARS
        rep stosb
        jmp main_loop

exit_app:
        ; Stop audio
        mov eax, SYS_AUDIO_STOP
        int 0x80
        mov eax, [win_id]
        call gui_destroy_window
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80


;=======================================================================
; Audio functions
;=======================================================================

start_audio:
        PUSHALL
        ; Calculate remaining data
        mov eax, [pcm_total_len]
        sub eax, [pcm_played]
        jbe .sa_done
        ; Chunk size: min(remaining, 32KB)
        cmp eax, 32768
        jbe .sa_size_ok
        mov eax, 32768
.sa_size_ok:
        mov [pcm_chunk_len], eax

        ; Play chunk
        mov ecx, eax            ; length
        mov ebx, [pcm_data_ptr]
        add ebx, [pcm_played]   ; buffer start
        mov edx, [wav_format]   ; format flags + sample rate
        mov eax, SYS_AUDIO_PLAY
        int 0x80

        ; Update played offset
        mov eax, [pcm_chunk_len]
        add [pcm_played], eax

        mov byte [play_state], STATE_PLAYING

        ; Generate fake VU levels from PCM data
        call update_vu_from_pcm

.sa_done:
        ; Check if fully played
        mov eax, [pcm_played]
        cmp eax, [pcm_total_len]
        jb .sa_not_done
        mov byte [play_state], STATE_STOPPED
        mov dword [pcm_played], 0
.sa_not_done:
        POPALL
        ret

; Update VU bars by sampling PCM data
update_vu_from_pcm:
        PUSHALL
        mov esi, [pcm_data_ptr]
        add esi, [pcm_played]
        ; Back up a bit to sample recent data
        sub esi, 1024
        cmp esi, [pcm_data_ptr]
        jge .uv_ok
        mov esi, [pcm_data_ptr]
.uv_ok:
        ; Sample VU_BARS points
        xor ecx, ecx
.uv_loop:
        cmp ecx, VU_BARS
        jge .uv_done
        ; Read sample (8-bit unsigned or 16-bit signed)
        test dword [wav_format], SB_FMT_16BIT
        jnz .uv_16bit
        ; 8-bit
        movzx eax, byte [esi]
        sub eax, 128            ; center at 0
        jns .uv_abs8
        neg eax
.uv_abs8:
        shr eax, 1              ; scale to 0-64
        jmp .uv_store
.uv_16bit:
        movsx eax, word [esi]
        test eax, eax
        jns .uv_abs16
        neg eax
.uv_abs16:
        shr eax, 9              ; scale 32768 → ~64
.uv_store:
        cmp eax, VU_BAR_H
        jbe .uv_clamp
        mov eax, VU_BAR_H
.uv_clamp:
        ; Only update if higher than current (peak hold)
        cmp al, [vu_levels + ecx]
        jbe .uv_no_update
        mov [vu_levels + ecx], al
.uv_no_update:
        ; Advance sample pointer
        add esi, 64             ; skip ahead
        inc ecx
        jmp .uv_loop
.uv_done:
        POPALL
        ret


;=======================================================================
; WAV file loader
;=======================================================================
load_wav:
        PUSHALL

        ; Read file
        mov ebx, arg_buf
        mov ecx, file_buf
        mov eax, SYS_FREAD
        int 0x80
        cmp eax, 0
        jle .lw_fail

        mov [file_size], eax

        ; Copy filename for display
        mov esi, arg_buf
        mov edi, filename
        xor ecx, ecx
.lw_copy_name:
        mov al, [esi + ecx]
        mov [edi + ecx], al
        inc ecx
        cmp al, 0
        jne .lw_copy_name

        ; Parse WAV header
        ; Check "RIFF" magic
        cmp dword [file_buf], 'RIFF'
        jne .lw_bad_fmt
        ; Check "WAVE" format
        cmp dword [file_buf + 8], 'WAVE'
        jne .lw_bad_fmt
        ; Check "fmt " sub-chunk
        cmp dword [file_buf + 12], 'fmt '
        jne .lw_bad_fmt

        ; Read format
        movzx eax, word [file_buf + 20]    ; audio format (1=PCM)
        cmp eax, 1
        jne .lw_bad_fmt

        movzx eax, word [file_buf + 22]    ; channels
        mov [wav_channels], eax
        mov eax, [file_buf + 24]            ; sample rate
        mov [wav_sample_rate], eax
        movzx eax, word [file_buf + 34]    ; bits per sample
        mov [wav_bits], eax

        ; Build format flags
        mov edx, [wav_sample_rate]       ; low 16 bits = sample rate
        and edx, 0xFFFF
        cmp dword [wav_bits], 16
        jne .lw_not_16
        or edx, SB_FMT_16BIT
        or edx, SB_FMT_SIGNED
.lw_not_16:
        cmp dword [wav_channels], 2
        jne .lw_not_stereo
        or edx, SB_FMT_STEREO
.lw_not_stereo:
        mov [wav_format], edx

        ; Find "data" sub-chunk
        mov esi, file_buf
        add esi, 12             ; start of sub-chunks
.lw_find_data:
        cmp dword [esi], 'data'
        je .lw_found_data
        ; Skip this chunk
        mov eax, [esi + 4]     ; chunk size
        add esi, 8
        add esi, eax
        ; Bounds check
        mov eax, esi
        sub eax, file_buf
        cmp eax, [file_size]
        jge .lw_bad_fmt
        jmp .lw_find_data

.lw_found_data:
        mov eax, [esi + 4]     ; data size
        mov [pcm_total_len], eax
        add esi, 8
        mov [pcm_data_ptr], esi

        ; Build info line
        call build_info_line

        mov byte [loaded], 1
        mov dword [pcm_played], 0
        mov byte [play_state], STATE_STOPPED

        POPALL
        ret

.lw_fail:
.lw_bad_fmt:
        mov byte [loaded], 0
        POPALL
        ret


; Build info string: "44100 Hz, 16-bit, Stereo, 123 KB"
build_info_line:
        PUSHALL
        mov edi, info_line

        ; Sample rate
        mov eax, [wav_sample_rate]
        call itoa_append
        mov esi, str_hz
        call str_copy_append

        ; Bits
        mov eax, [wav_bits]
        call itoa_append
        mov esi, str_bit
        call str_copy_append

        ; Channels
        cmp dword [wav_channels], 2
        jne .bil_mono
        mov esi, str_stereo
        jmp .bil_chan
.bil_mono:
        mov esi, str_mono
.bil_chan:
        call str_copy_append

        ; Size in KB
        mov eax, [pcm_total_len]
        shr eax, 10
        call itoa_append
        mov esi, str_kb
        call str_copy_append

        mov byte [edi], 0
        POPALL
        ret

; itoa_append - Convert EAX to decimal string at EDI, advance EDI
itoa_append:
        PUSHALL
        mov ebx, 10
        xor ecx, ecx
        test eax, eax
        jnz .ia_loop
        mov byte [edi], '0'
        inc edi
        mov [rsp + 112], edi     ; update saved edi
        POPALL
        ret
.ia_loop:
        test eax, eax
        jz .ia_reverse
        xor edx, edx
        div ebx
        add dl, '0'
        push rdx
        inc ecx
        jmp .ia_loop
.ia_reverse:
        test ecx, ecx
        jz .ia_done
        pop rax
        mov [edi], al
        inc edi
        dec ecx
        jmp .ia_reverse
.ia_done:
        mov [rsp + 112], edi     ; update saved edi
        POPALL
        ret

; str_copy_append - Copy string at ESI to EDI, advance EDI
str_copy_append:
        push rax
.sca_loop:
        lodsb
        test al, al
        jz .sca_done
        stosb
        jmp .sca_loop
.sca_done:
        pop rax
        ret


;=======================================================================
; Data
;=======================================================================
title_str:      db "BPlayer - Audio", 0
str_no_file:    db "No file loaded", 0
str_play:       db "Play", 0
str_pause:      db "Pause", 0
str_stop:       db "Stop", 0
str_status_stopped: db "Stopped", 0
str_status_playing: db "Playing", 0
str_status_paused:  db "Paused", 0
str_hz:         db " Hz, ", 0
str_bit:        db "-bit, ", 0
str_stereo:     db "Stereo, ", 0
str_mono:       db "Mono, ", 0
str_kb:         db " KB", 0

win_id:         dd 0
play_state:     db STATE_STOPPED
loaded:         db 0
filename:       times 64 db 0
info_line:      times 80 db 0

; WAV metadata
wav_channels:   dd 0
wav_sample_rate: dd 0
wav_bits:       dd 0
wav_format:     dd 0            ; format flags for SYS_AUDIO_PLAY

; Playback state
pcm_data_ptr:   dd 0            ; pointer into file_buf
pcm_total_len:  dd 0
pcm_played:     dd 0
pcm_chunk_len:  dd 0

; VU meter
vu_levels:      times VU_BARS db 0

arg_buf:        times 256 db 0
file_buf:       times FILE_BUF_SIZE db 0
file_size:      dd 0
