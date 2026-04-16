;=======================================================================
; sb16.inc - Sound Blaster 16 Audio Driver
;
; Provides PCM audio playback via the Sound Blaster 16 (ISA DMA).
; Supports 8-bit and 16-bit PCM, mono/stereo, 8000-44100 Hz.
;
; I/O Ports (base 0x220):
;   0x220+0x06 = DSP Reset
;   0x220+0x0A = DSP Read Data
;   0x220+0x0C = DSP Write Data/Command
;   0x220+0x0E = DSP Read Status (bit 7 = data available)
;   0x220+0x0F = DSP 16-bit ACK
;
; DMA Channels:
;   Channel 1 = 8-bit transfers (ports 0x00-0x0F, page 0x83)
;   Channel 5 = 16-bit transfers (ports 0xC0-0xCF, page 0x8B)
;
; IRQ: 5 (default), INT vector 0x25
;
; Syscalls:
;   SYS_AUDIO_PLAY  (50) - Start playback: EBX=buf ECX=len EDX=fmt
;   SYS_AUDIO_STOP  (51) - Stop playback
;   SYS_AUDIO_STATUS(52) - Query: EAX=0 idle, 1=playing, 2=done
;=======================================================================

; Sound Blaster 16 base port
SB16_BASE       equ 0x220

; DSP port offsets from SB16_BASE
SB_RESET        equ SB16_BASE + 0x06
SB_READ         equ SB16_BASE + 0x0A
SB_WRITE        equ SB16_BASE + 0x0C
SB_WRITE_STATUS equ SB16_BASE + 0x0C
SB_READ_STATUS  equ SB16_BASE + 0x0E
SB_ACK_16       equ SB16_BASE + 0x0F

; DSP commands
SB_CMD_SET_RATE     equ 0x41   ; Set output sample rate
SB_CMD_PLAY_8       equ 0xC0   ; 8-bit single-cycle DMA output
SB_CMD_PLAY_16      equ 0xB0   ; 16-bit single-cycle DMA output
SB_CMD_AUTO_8       equ 0xC6   ; 8-bit auto-init DMA output
SB_CMD_AUTO_16      equ 0xB6   ; 16-bit auto-init DMA output
SB_CMD_STOP_8       equ 0xD0   ; Pause 8-bit DMA
SB_CMD_RESUME_8     equ 0xD4   ; Resume 8-bit DMA
SB_CMD_STOP_16      equ 0xD5   ; Pause 16-bit DMA
SB_CMD_RESUME_16    equ 0xD6   ; Resume 16-bit DMA
SB_CMD_EXIT_AUTO_16 equ 0xD9   ; Exit 16-bit auto-init DMA
SB_CMD_EXIT_AUTO_8  equ 0xDA   ; Exit 8-bit auto-init DMA
SB_CMD_SPEAKER_ON   equ 0xD1   ; Enable speaker
SB_CMD_SPEAKER_OFF  equ 0xD3   ; Disable speaker
SB_CMD_GET_VERSION  equ 0xE1   ; Get DSP version

; DMA mode bits for 8/16-bit transfer sub-commands
SB_MODE_SIGNED      equ 0x10   ; Signed data
SB_MODE_STEREO      equ 0x20   ; Stereo data
SB_MODE_UNSIGNED    equ 0x00   ; Unsigned data
SB_MODE_MONO        equ 0x00   ; Mono data

; 8-bit DMA channel 1 ports
DMA1_MASK       equ 0x0A       ; Single mask register
DMA1_MODE       equ 0x0B       ; Mode register
DMA1_FLIPFLOP   equ 0x0C       ; Byte flip-flop clear
DMA1_ADDR       equ 0x02       ; Channel 1 address (base is ch*2)
DMA1_COUNT      equ 0x03       ; Channel 1 count
DMA1_PAGE       equ 0x83       ; Channel 1 page register

; 16-bit DMA channel 5 ports
DMA5_MASK       equ 0xD4       ; Single mask register
DMA5_MODE       equ 0xD6       ; Mode register
DMA5_FLIPFLOP   equ 0xD8       ; Byte flip-flop clear
DMA5_ADDR       equ 0xC4       ; Channel 5 address (base is (ch-4)*4)
DMA5_COUNT      equ 0xC6       ; Channel 5 count
DMA5_PAGE       equ 0x8B       ; Channel 5 page register

; Audio playback states
SB_STATE_IDLE       equ 0
SB_STATE_PLAYING    equ 1
SB_STATE_DONE       equ 2

; Audio format flags (passed in EDX of SYS_AUDIO_PLAY)
; Bits 0-15: sample rate (Hz)
; Bit 16: 0=8-bit, 1=16-bit
; Bit 17: 0=mono, 1=stereo
; Bit 18: 0=unsigned, 1=signed
SB_FMT_16BIT    equ (1 << 16)
SB_FMT_STEREO   equ (1 << 17)
SB_FMT_SIGNED   equ (1 << 18)

;---------------------------------------
; sb16_init - Detect and initialize SB16
; Called during kernel init.
; Sets sb16_present=1 if found, else 0.
;---------------------------------------
sb16_init:
        PUSHALL
        mov byte [sb16_present], 0
        mov byte [sb16_state], SB_STATE_IDLE

        ; Reset DSP: write 1, wait, write 0, wait for 0xAA
        mov dx, SB_RESET
        mov al, 1
        out dx, al

        ; Wait ~3 microseconds (loop)
        mov ecx, 100
.reset_wait:
        in al, 0x80             ; I/O delay
        dec ecx
        jnz .reset_wait

        xor al, al
        out dx, al

        ; Wait for DSP to send 0xAA (ready signal)
        mov ecx, 0xFFFF
.ready_loop:
        mov dx, SB_READ_STATUS
        in al, dx
        test al, 0x80           ; Data available?
        jnz .read_ready
        dec ecx
        jnz .ready_loop
        jmp .sb16_not_found     ; Timeout

.read_ready:
        mov dx, SB_READ
        in al, dx
        cmp al, 0xAA
        jne .sb16_not_found

        ; DSP found! Get version
        call sb16_dsp_write_wait
        mov dx, SB_WRITE
        mov al, SB_CMD_GET_VERSION
        out dx, al

        ; Read major version
        call sb16_dsp_read_wait
        mov dx, SB_READ
        in al, dx
        mov [sb16_version_major], al
        movzx eax, al

        ; Read minor version
        call sb16_dsp_read_wait
        mov dx, SB_READ
        in al, dx
        mov [sb16_version_minor], al

        ; Check for DSP version >= 4.00 (SB16 requirement)
        cmp byte [sb16_version_major], 4
        jb .sb16_not_found

        ; Enable speaker
        call sb16_dsp_write_wait
        mov dx, SB_WRITE
        mov al, SB_CMD_SPEAKER_ON
        out dx, al

        ; Install IRQ5 handler (INT 0x25)
        mov ecx, 0x25
        mov eax, irq_sb16
        call idt_set_gate

        ; Unmask IRQ5 on PIC1 (bit 5)
        in al, PIC1_DATA
        and al, ~(1 << 5)
        out PIC1_DATA, al

        mov byte [sb16_present], 1

        ; Print detection message
        mov esi, sb16_msg_found
        call vga_print

        POPALL
        ret

.sb16_not_found:
        mov esi, sb16_msg_none
        call vga_print
        POPALL
        ret

;---------------------------------------
; sb16_dsp_write_wait - Wait until DSP
; is ready to accept a command byte.
;---------------------------------------
sb16_dsp_write_wait:
        push rax
        push rdx
        mov edx, SB_WRITE_STATUS
.dsp_ww:
        in al, dx
        test al, 0x80
        jnz .dsp_ww
        pop rdx
        pop rax
        ret

;---------------------------------------
; sb16_dsp_read_wait - Wait until DSP
; has data ready to read.
;---------------------------------------
sb16_dsp_read_wait:
        push rax
        push rdx
        mov edx, SB_READ_STATUS
.dsp_rw:
        in al, dx
        test al, 0x80
        jz .dsp_rw
        pop rdx
        pop rax
        ret

;---------------------------------------
; sb16_dsp_write - Write command/data
;   AL = byte to write
;---------------------------------------
sb16_dsp_write:
        push rdx
        push rax
        call sb16_dsp_write_wait
        pop rax
        mov edx, SB_WRITE
        out dx, al
        pop rdx
        ret

;---------------------------------------
; sb16_play - Start PCM audio playback
;   EBX = pointer to PCM sample data
;   ECX = length in bytes
;   EDX = format (bits 0-15=rate Hz,
;         bit16=16-bit, bit17=stereo,
;         bit18=signed)
;
; The buffer MUST be in low memory (under
; 16 MB for ISA DMA) and must not cross
; a 64 KB boundary.
;---------------------------------------
sb16_play:
        PUSHALL

        cmp byte [sb16_present], 0
        je .play_done

        ; Save playback parameters
        mov [sb16_buf_addr], ebx
        mov [sb16_buf_len], ecx
        mov [sb16_format], edx
        mov byte [sb16_state], SB_STATE_PLAYING

        ; Extract sample rate from low 16 bits
        movzx eax, dx
        test eax, eax
        jnz .rate_ok
        mov eax, 11025          ; Default rate if 0
.rate_ok:
        mov [sb16_sample_rate], eax

        ; Set DSP sample rate
        mov al, SB_CMD_SET_RATE
        call sb16_dsp_write
        mov eax, [sb16_sample_rate]
        shr eax, 8
        call sb16_dsp_write     ; High byte
        mov eax, [sb16_sample_rate]
        and al, 0xFF
        call sb16_dsp_write     ; Low byte

        ; Determine 8-bit or 16-bit transfer
        test dword [sb16_format], SB_FMT_16BIT
        jnz .play_16bit

        ;--- 8-bit DMA playback (channel 1) ---
        ; Disable channel 1
        mov al, 0x05            ; Mask channel 1 (bit2=mask, bits0-1=channel)
        out DMA1_MASK, al

        ; Clear byte flip-flop
        xor al, al
        out DMA1_FLIPFLOP, al

        ; Set DMA mode: single-cycle, read (memory->device), channel 1
        mov al, 0x49            ; 01=single, 00=read, 01=ch1
        out DMA1_MODE, al

        ; Set address (physical address of buffer)
        mov eax, [sb16_buf_addr]
        out DMA1_ADDR, al       ; Low byte
        shr eax, 8
        out DMA1_ADDR, al       ; High byte

        ; Set page register (bits 16-23 of physical address)
        mov eax, [sb16_buf_addr]
        shr eax, 16
        out DMA1_PAGE, al

        ; Set count (length - 1)
        mov eax, [sb16_buf_len]
        dec eax
        out DMA1_COUNT, al      ; Low byte
        shr eax, 8
        out DMA1_COUNT, al      ; High byte

        ; Enable channel 1
        mov al, 0x01            ; Unmask channel 1
        out DMA1_MASK, al

        ; Build mode byte for DSP command
        xor ebx, ebx
        test dword [sb16_format], SB_FMT_SIGNED
        jz .u8
        or bl, SB_MODE_SIGNED
.u8:
        test dword [sb16_format], SB_FMT_STEREO
        jz .m8
        or bl, SB_MODE_STEREO
.m8:

        ; Issue 8-bit single-cycle play command
        mov al, SB_CMD_PLAY_8
        call sb16_dsp_write
        mov al, bl              ; Mode byte
        call sb16_dsp_write

        ; Transfer length - 1 (low, high)
        mov eax, [sb16_buf_len]
        dec eax
        call sb16_dsp_write     ; Low byte
        shr eax, 8
        call sb16_dsp_write     ; High byte

        jmp .play_done

.play_16bit:
        ;--- 16-bit DMA playback (channel 5) ---
        ; Disable channel 5
        mov al, 0x05            ; Mask channel 5 (bit2=mask, bits0-1=ch 5-4=1)
        out DMA5_MASK, al

        ; Clear byte flip-flop
        xor al, al
        out DMA5_FLIPFLOP, al

        ; Set DMA mode: single-cycle, read, channel 5
        mov al, 0x49            ; 01=single, 00=read, 01=ch5-4=1
        out DMA5_MODE, al

        ; For 16-bit DMA, address is in words (physical address / 2)
        mov eax, [sb16_buf_addr]
        shr eax, 1
        out DMA5_ADDR, al       ; Low byte
        shr eax, 8
        out DMA5_ADDR, al       ; High byte

        ; Page register (bits 16-23 of physical address)
        mov eax, [sb16_buf_addr]
        shr eax, 16
        out DMA5_PAGE, al

        ; Count in 16-bit samples: (byte_length / 2) - 1
        mov eax, [sb16_buf_len]
        shr eax, 1
        dec eax
        out DMA5_COUNT, al
        shr eax, 8
        out DMA5_COUNT, al

        ; Enable channel 5
        mov al, 0x01
        out DMA5_MASK, al

        ; Build mode
        xor ebx, ebx
        test dword [sb16_format], SB_FMT_SIGNED
        jz .u16
        or bl, SB_MODE_SIGNED
.u16:
        test dword [sb16_format], SB_FMT_STEREO
        jz .m16
        or bl, SB_MODE_STEREO
.m16:

        ; Issue 16-bit single-cycle play command
        mov al, SB_CMD_PLAY_16
        call sb16_dsp_write
        mov al, bl              ; Mode byte
        call sb16_dsp_write

        ; Transfer length in samples - 1
        mov eax, [sb16_buf_len]
        shr eax, 1              ; Byte count -> sample count
        dec eax
        call sb16_dsp_write     ; Low byte
        shr eax, 8
        call sb16_dsp_write     ; High byte

.play_done:
        POPALL
        ret

;---------------------------------------
; sb16_stop - Stop current playback
;---------------------------------------
sb16_stop:
        PUSHALL
        cmp byte [sb16_present], 0
        je .stop_done
        cmp byte [sb16_state], SB_STATE_PLAYING
        jne .stop_done

        ; Pause DMA
        test dword [sb16_format], SB_FMT_16BIT
        jnz .stop_16
        mov al, SB_CMD_STOP_8
        call sb16_dsp_write
        jmp .stop_state
.stop_16:
        mov al, SB_CMD_STOP_16
        call sb16_dsp_write

.stop_state:
        mov byte [sb16_state], SB_STATE_IDLE
.stop_done:
        POPALL
        ret

;---------------------------------------
; irq_sb16 - IRQ5 handler
; Acknowledges the SB16 interrupt and
; marks playback as done.
;---------------------------------------
irq_sb16:
        PUSHALL

        ; Acknowledge SB16 interrupt by reading DSP status
        ; Check 16-bit first
        test dword [sb16_format], SB_FMT_16BIT
        jnz .ack16
        mov dx, SB_READ_STATUS  ; 8-bit ack
        in al, dx
        jmp .ack_done
.ack16:
        mov dx, SB_ACK_16       ; 16-bit ack
        in al, dx

.ack_done:
        ; Mark playback complete
        mov byte [sb16_state], SB_STATE_DONE

        ; Send EOI to PIC1 (IRQ5 is on PIC1)
        mov al, 0x20
        out PIC1_CMD, al

        POPALL
        iretq

;---------------------------------------
; sys_audio_play - SYS_AUDIO_PLAY handler
;   EBX = buffer pointer
;   ECX = length in bytes
;   EDX = format word
; Returns EAX = 0 on success, -1 if no SB16
;---------------------------------------
sys_audio_play:
        cmp byte [sb16_present], 0
        je .sap_fail
        call sb16_play
        xor eax, eax
        iretq
.sap_fail:
        mov eax, -1
        iretq

;---------------------------------------
; sys_audio_stop - SYS_AUDIO_STOP handler
; Returns EAX = 0
;---------------------------------------
sys_audio_stop:
        call sb16_stop
        xor eax, eax
        iretq

;---------------------------------------
; sys_audio_status - SYS_AUDIO_STATUS
; Returns EAX = sb16_state
;   0=idle, 1=playing, 2=done
; Also returns EBX = sb16_present
;---------------------------------------
sys_audio_status:
        movzx eax, byte [sb16_state]
        movzx ebx, byte [sb16_present]
        iretq

;---------------------------------------
; sb16_parse_wav - Parse WAV header
;   ESI = pointer to WAV file data
;   ECX = total file size
; Returns:
;   EAX = pointer to PCM data start
;   EBX = PCM data length (bytes)
;   EDX = format word for sb16_play
;   CF set on error
;---------------------------------------
sb16_parse_wav:
        PUSHALL

        ; Check minimum size for WAV header (44 bytes)
        cmp ecx, 44
        jb .wav_error

        ; Verify RIFF header magic "RIFF"
        cmp dword [esi], 0x46464952     ; 'RIFF'
        jne .wav_error

        ; Verify WAVE format "WAVE"
        cmp dword [esi + 8], 0x45564157 ; 'WAVE'
        jne .wav_error

        ; Find 'fmt ' chunk (usually at offset 12)
        cmp dword [esi + 12], 0x20746D66 ; 'fmt '
        jne .wav_error

        ; fmt chunk data starts at offset 20
        ; Offset 20: AudioFormat (1 = PCM, 2 bytes)
        movzx eax, word [esi + 20]
        cmp eax, 1              ; Must be PCM (uncompressed)
        jne .wav_error

        ; Offset 22: NumChannels (1=mono, 2=stereo)
        movzx eax, word [esi + 22]
        mov [.wav_channels], eax

        ; Offset 24: SampleRate (4 bytes)
        mov eax, [esi + 24]
        mov [.wav_rate], eax

        ; Offset 34: BitsPerSample (8 or 16)
        movzx eax, word [esi + 34]
        mov [.wav_bits], eax

        ; Find 'data' chunk — scan from offset 36
        lea edi, [esi + 36]
        lea ebx, [esi + ecx]    ; End of file
.find_data:
        cmp edi, ebx
        jge .wav_error
        cmp dword [edi], 0x61746164     ; 'data'
        je .found_data
        add edi, 1
        jmp .find_data

.found_data:
        ; Data chunk size at [edi + 4]
        mov ecx, [edi + 4]     ; PCM data length
        add edi, 8              ; Skip 'data' + size field = PCM start

        ; Build format word
        movzx edx, word [.wav_rate]
        ; Add high bits from rate if > 65535 (e.g. 44100)
        mov eax, [.wav_rate]
        and eax, 0xFFFF
        mov edx, eax

        cmp dword [.wav_bits], 16
        jne .wav_not16
        or edx, SB_FMT_16BIT
        or edx, SB_FMT_SIGNED  ; 16-bit WAV is always signed
.wav_not16:
        cmp dword [.wav_channels], 2
        jne .wav_not_stereo
        or edx, SB_FMT_STEREO
.wav_not_stereo:

        ; Return values via PUSHALL frame
        ; EAX slot at [rsp+28], EBX at [rsp+16], EDX at [rsp+20]
        mov [rsp + FRAME_RAX], rdi     ; RAX = PCM data pointer
        mov [rsp + FRAME_RBX], rcx     ; RBX = PCM data length
        mov [rsp + FRAME_RDX], rdx     ; RDX = format word

        POPALL
        clc                     ; Success
        ret

.wav_error:
        POPALL
        stc                     ; Error
        ret

; Local storage for WAV parser
.wav_channels:  dd 0
.wav_rate:      dd 0
.wav_bits:      dd 0

;---------------------------------------
; Data strings for SB16
;---------------------------------------
sb16_msg_found: db "  Sound Blaster 16 detected", 0x0A, 0
sb16_msg_none:  db "  Sound Blaster 16 not found (PC speaker only)", 0x0A, 0
