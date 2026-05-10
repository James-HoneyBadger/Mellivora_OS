; rec.asm - Audio Recorder (v9.0)
; Records from line-in via AC97 PCM IN and saves as WAV file.
; Usage: rec <filename> [seconds]
; Records 16-bit stereo PCM at 44100 Hz.

%include "syscalls.inc"

WAV_SAMPLE_RATE  equ 44100
WAV_CHANNELS     equ 2
WAV_BITS         equ 16
WAV_BLK_ALIGN    equ WAV_CHANNELS * (WAV_BITS / 8)   ; 4 bytes/sample
WAV_BYTE_RATE    equ WAV_SAMPLE_RATE * WAV_BLK_ALIGN  ; 176400 bytes/sec
REC_BUF_SIZE     equ 176400 * 3   ; 3 seconds max @ 44100 Hz stereo 16-bit = 529200 bytes
; Limit to demand-paged user memory capacity (512 KB safe in user space)
REC_BUF_SAFE     equ 512000       ; 500 KB ≈ 2.9 seconds (fits two copies in < 1 MB binary)

start:
        ; Parse arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        mov [arg_len], eax

        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Copy filename
        mov edi, out_filename
        mov ecx, 127
.cp_fname:
        lodsb
        cmp al, ' '
        je .fname_done
        cmp al, 0
        je .fname_done
        stosb
        dec ecx
        jnz .cp_fname
.fname_done:
        mov byte [edi], 0

        ; Optional: seconds argument
        call skip_spaces
        cmp byte [esi], 0
        je .use_default_secs
        call parse_uint
        cmp eax, 0
        je .use_default_secs
        cmp eax, 10
        jle .secs_ok
        mov eax, 10             ; max 10 seconds
.secs_ok:
        mov [rec_secs], eax
        jmp .start_rec
.use_default_secs:
        mov dword [rec_secs], 3

.start_rec:
        ; Compute byte count = secs * WAV_BYTE_RATE (capped at REC_BUF_SAFE)
        mov eax, [rec_secs]
        imul eax, WAV_BYTE_RATE
        cmp eax, REC_BUF_SAFE
        jle .buf_ok
        mov eax, REC_BUF_SAFE
.buf_ok:
        mov [rec_bytes], eax

        ; Print start message
        mov eax, SYS_PRINT
        mov ebx, msg_recording
        int 0x80

        ; Start recording
        mov eax, SYS_AUDIO_REC_START
        mov ebx, rec_buf        ; buffer in BSS
        mov ecx, [rec_bytes]
        int 0x80
        cmp eax, -1
        je .rec_fail

        ; Wait for recording: sleep for rec_secs * 1000ms
        mov eax, SYS_SLEEP
        mov ebx, [rec_secs]
        imul ebx, 1000          ; ms
        int 0x80

        ; Stop recording and copy data
        mov eax, SYS_AUDIO_REC_STOP
        int 0x80

        ; Build WAV header, then copy PCM data after it to form complete WAV
        call build_wav_header

        ; Copy PCM data into wav_buf right after the header
        ; (rec_buf contains the raw PCM; wav_header is the 44-byte header)
        ; For simplicity write header + PCM as one contiguous buffer by
        ; moving the PCM into a combined buffer.  Since rec_buf is in BSS
        ; and wav_out_buf is also in BSS, do a manual memmove.
        mov esi, rec_buf
        mov edi, wav_out_buf + 44
        mov ecx, [rec_bytes]
        rep movsb
        ; Copy header to start of wav_out_buf
        mov esi, wav_header
        mov edi, wav_out_buf
        mov ecx, 44
        rep movsb

        ; Write combined WAV file
        mov eax, SYS_FWRITE
        mov ebx, out_filename
        mov ecx, wav_out_buf
        mov edx, [rec_bytes]
        add edx, 44
        xor esi, esi            ; type 0 = binary
        int 0x80
        cmp eax, -1
        je .create_fail

        ; Success message
        mov eax, SYS_PRINT
        mov ebx, msg_saved
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, out_filename
        int 0x80
        mov eax, SYS_PUTCHAR
        mov bl, 0x0A
        int 0x80
        jmp .exit

.rec_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_rec_fail
        int 0x80
        jmp .exit

.create_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_create_fail
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; build_wav_header - Fill wav_header with RIFF WAV header
;---------------------------------------
build_wav_header:
        pushad
        mov edi, wav_header
        ; RIFF chunk
        mov dword [edi],    0x46464952  ; "RIFF"
        mov eax, [rec_bytes]
        add eax, 36
        mov [edi + 4],  eax             ; ChunkSize = data_size + 36
        mov dword [edi + 8],  0x45564157  ; "WAVE"
        ; fmt sub-chunk
        mov dword [edi + 12], 0x20746D66  ; "fmt "
        mov dword [edi + 16], 16          ; Subchunk1Size = 16 (PCM)
        mov word  [edi + 20], 1           ; AudioFormat = 1 (PCM)
        mov word  [edi + 22], WAV_CHANNELS
        mov dword [edi + 24], WAV_SAMPLE_RATE
        mov dword [edi + 28], WAV_BYTE_RATE
        mov word  [edi + 32], WAV_BLK_ALIGN
        mov word  [edi + 34], WAV_BITS
        ; data sub-chunk
        mov dword [edi + 36], 0x61746164  ; "data"
        mov eax, [rec_bytes]
        mov [edi + 40], eax               ; Subchunk2Size
        popad
        ret

;---------------------------------------
; skip_spaces - advance ESI past spaces
;---------------------------------------
skip_spaces:
        lodsb
        cmp al, ' '
        je skip_spaces
        dec esi
        ret

;---------------------------------------
; parse_uint - parse decimal from ESI, returns EAX
;---------------------------------------
parse_uint:
        xor eax, eax
.pu_loop:
        movzx ecx, byte [esi]
        cmp cl, '0'
        jl .pu_done
        cmp cl, '9'
        jg .pu_done
        imul eax, 10
        sub ecx, '0'
        add eax, ecx
        inc esi
        jmp .pu_loop
.pu_done:
        ret

;=======================================================================
; DATA
;=======================================================================
msg_usage:       db "Usage: rec <filename.wav> [seconds]", 0x0A,
                 db "  Records audio from line-in (max 10 seconds)", 0x0A, 0
msg_recording:   db "Recording... (press nothing, wait)", 0x0A, 0
msg_saved:       db "Saved: ", 0
msg_rec_fail:    db "Error: AC97 recording not available", 0x0A, 0
msg_create_fail: db "Error: cannot create output file", 0x0A, 0

out_filename:    times 128 db 0
arg_buf:         times 256 db 0
arg_len:         dd 0
rec_secs:        dd 3
rec_bytes:       dd 0

wav_header:      times 44 db 0

; Recording buffer (at end of BSS — demand-paged via v9.0 paging)
rec_buf:         times REC_BUF_SAFE db 0
; Combined WAV output buffer (header + PCM)
wav_out_buf:     times REC_BUF_SAFE + 44 db 0
