;=======================================================================
; cmod.inc - the bridge into the C-built kernel module
;
; The module is freestanding 32-bit C (c/kmod/kmod.c). It is compiled and
; linked on its own, at the address the assembler reports for the cmod slot at
; the end of data.inc, and then assembled into the kernel image with incbin.
; Because nothing relocates it, it has no undefined symbols: everything it may
; call arrives in the host table below, and the header it publishes is read by
; number here.
;
; Both structures are defined in c/include/mv/cmod.h, where every offset and
; size is asserted at compile time. The numbers below are the assembly half of
; that contract; if they ever disagree the module is refused at load, so the
; failure is "CMOD_FAIL: header" at boot rather than a call into nothing.
;=======================================================================

%ifndef CMOD_INC
%define CMOD_INC

; Module header, at offset 0 of the blob.
CMS_MAGIC       equ 0x444F4D43      ; 'CMOD' little-endian
CMS_VERSION     equ 1
CMS_SIZE        equ 24              ; sizeof(struct cmod_header)
CMS_OFF_MAGIC   equ 0
CMS_OFF_VERSION equ 4
CMS_OFF_SIZE    equ 8
CMS_OFF_INIT    equ 12
CMS_OFF_TEST    equ 16
CMS_OFF_NAME    equ 20

; Host table, passed to init().
CMH_VERSION     equ 1
CMH_SIZE        equ 16              ; sizeof(struct cmod_host)

;===============================
; The host half of the ABI
;===============================
cmod_host_table:
        dd CMH_VERSION              ; api_version
        dd CMH_SIZE                 ; size, checked by the module's init
        dd cmod_host_add            ; u32 (*add)(u32, u32)
        dd cmod_host_print          ; void (*print)(const char *)

;---------------------------------------
; cmod_host_print - vga_print with the C argument convention
;
; vga_print reads its string from ESI; a cdecl call passes it on the stack.
; The push keeps the argument at [esp+8] where vga_print's own pushad expects
; it, and vga_print preserves every register, so ESI is put back afterwards.
;---------------------------------------
cmod_host_print:
        push esi
        mov esi, [esp + 8]
        call vga_print
        pop esi
        ret


;---------------------------------------
; cmod_host_add - the assembly side of the boundary
;
; cdecl, exactly as docs/spec/ABI.md section 2 describes it: arguments at
; [esp+4] and [esp+8], result in EAX, EBX preserved for the caller. The module
; calls it through a function pointer, so this is the one path that proves
; C can call assembly and not only the other way round.
;---------------------------------------
cmod_host_add:
        push ebx
        mov eax, [esp + 8]
        mov ebx, [esp + 12]
        add eax, ebx
        pop ebx
        ret

;---------------------------------------
; cmod_load - is a module linked in, and does it accept our host table?
;   Out: EAX = 0 loaded, 1 absent or refused
;   Preserves every register.
;
; init() returning nonzero means the module was built against a different
; header or API version and would misread the table, so a refusal is final.
;---------------------------------------
cmod_load:
        pushad
        mov esi, cmod_blob
        cmp dword [esi + CMS_OFF_MAGIC], CMS_MAGIC
        jne .cl_absent
        cmp dword [esi + CMS_OFF_VERSION], CMS_VERSION
        jb .cl_absent
        cmp dword [esi + CMS_OFF_SIZE], CMS_SIZE
        jb .cl_absent
        mov eax, [esi + CMS_OFF_INIT]
        test eax, eax
        jz .cl_absent
        cld                         ; the C ABI requires DF clear on entry
        push cmod_host_table
        call eax
        add esp, 4
        test eax, eax
        jnz .cl_absent
        mov dword [cmod_ready], 1
        xor ebx, ebx
        jmp .cl_done
.cl_absent:
        mov dword [cmod_ready], 0
        mov ebx, 1
.cl_done:
        mov [esp + 28], ebx         ; popad's saved EAX slot
        popad
        ret

;---------------------------------------
; cmod_selftest - run the module's own checks and print the verdict
;   Prints CMOD_OK, CMOD_FAIL <code> or CMOD_ABSENT. Preserves every register.
;---------------------------------------
cmod_selftest:
        pushad
        call cmod_load
        test eax, eax
        jnz .cs_absent

        mov esi, cmod_blob
        mov eax, [esi + CMS_OFF_TEST]
        test eax, eax
        jz .cs_fail
        cld
        call eax
        test eax, eax
        jnz .cs_fail_rc

        mov esi, cmod_ok_msg
        call vga_print
        jmp .cs_done

.cs_fail_rc:
        mov [.cs_code], eax
        mov esi, cmod_fail_msg
        call vga_print
        mov eax, [.cs_code]
        call vga_print_dec
        mov al, 10
        call vga_putchar
        jmp .cs_done

.cs_fail:
        mov esi, cmod_fail_msg
        call vga_print
        mov al, '?'
        call vga_putchar
        mov al, 10
        call vga_putchar
        jmp .cs_done

.cs_absent:
        mov esi, cmod_absent_msg
        call vga_print

.cs_done:
        popad
        ret

.cs_code:       dd 0

;---------------------------------------
; cmod_boot - say so at boot when no module is linked in
;
; A successful load is silent here: every boot would otherwise print the same
; line, and `cmod` is the command that proves it. An image built without a C
; toolchain must not look like one that has a module, though, so absence is
; reported once, early, on the console.
;---------------------------------------
cmod_boot:
        pushad
        call cmod_load
        test eax, eax
        jz .cb_done
        mov esi, cmod_absent_boot_msg
        call vga_print
.cb_done:
        popad
        ret

cmod_ok_msg:     db "CMOD_OK", 10, 0
cmod_fail_msg:   db "CMOD_FAIL ", 0
cmod_absent_msg: db "CMOD_ABSENT: this image was built without a C toolchain", 10, 0
cmod_absent_boot_msg: db "cmodule: absent (built without a C toolchain)", 10, 0

%endif ; CMOD_INC
