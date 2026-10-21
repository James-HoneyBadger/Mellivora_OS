;=======================================================================
; kmem.inc - kernel slab allocator
;
; Kernel objects that exist once per task (its mmap area table, its argv
; snapshot) used to live in arrays sized for MAX_TASKS, so the image carried
; 128 copies of each whether or not any task was alive - 180 KB of BSS out of a
; 2 MB kernel. A slab cache hands out one fixed-size object at a time and takes
; a 4 KB page from the PMM only when it runs out, so an idle system pays
; nothing and a busy one pays for the objects that actually exist.
;
; A cache is described by its object size and how many objects fit in a page.
; Free objects are threaded on an intrusive list: the first dword of a free
; object holds the address of the next one, so no metadata is stored outside
; the objects themselves. Freed objects are NOT returned to the PMM - a cache
; keeps its pages and reuses them, which is the point of a slab.
;
; Every object is zeroed before it is handed out, so a recycled slot can never
; leak the previous owner's contents (that mattered: a task slot is reused the
; moment its predecessor exits).
;
;   kmem_init             - reset every cache and create the built-in ones
;   kmem_cache  EAX=size  - register a cache; EAX = cache id, or -1
;   kmem_alloc  EAX=id    - EAX = zeroed object, or 0 when out of memory
;   kmem_free   EAX=id, EBX=object
;
; kmem_alloc and kmem_free preserve every other register.
;=======================================================================

KMEM_CACHES     equ 4
KMEM_PAGE_SIZE  equ 4096
KMEM_FREE_END   equ 0xFFFFFFFF      ; empty free list
KMEM_ARGS_SIZE  equ 512             ; one task's argv snapshot

;-----------------------------------------------------------------------
; kmem_init - reset the cache table and create the kernel's caches
;-----------------------------------------------------------------------
kmem_init:
        push eax
        push ecx
        push edi
        mov edi, kmem_obj_size
        xor eax, eax
        mov ecx, KMEM_CACHES
        cld
        rep stosd                       ; object sizes
        mov ecx, KMEM_CACHES
        rep stosd                       ; objects per slab
        mov ecx, KMEM_CACHES
        rep stosd                       ; free-list heads
        pop edi
        pop ecx
        pop eax

        mov eax, VM_AREAS_PER_TASK * VM_AREA_SIZE
        call kmem_cache
        mov [cache_vm_area], eax
        mov eax, KMEM_ARGS_SIZE
        call kmem_cache
        mov [cache_task_args], eax
        ret

;-----------------------------------------------------------------------
; kmem_cache - register a cache for fixed-size objects
;   In : EAX = object size in bytes
;   Out: EAX = cache id (0..KMEM_CACHES-1), or -1 when it does not fit
;-----------------------------------------------------------------------
kmem_cache:
        push ecx
        push edx
        add eax, 3
        and eax, ~3                     ; the free link lives in the object
        jz .kc_fail
        cmp eax, KMEM_PAGE_SIZE
        ja .kc_fail

        xor ecx, ecx
.kc_find:
        cmp ecx, KMEM_CACHES
        jae .kc_fail
        cmp dword [kmem_obj_size + ecx*4], 0
        je .kc_take
        inc ecx
        jmp .kc_find

.kc_take:
        mov [kmem_obj_size + ecx*4], eax
        mov eax, KMEM_PAGE_SIZE
        xor edx, edx
        div dword [kmem_obj_size + ecx*4]
        mov [kmem_per_slab + ecx*4], eax
        mov dword [kmem_free_list + ecx*4], KMEM_FREE_END
        mov eax, ecx
        pop edx
        pop ecx
        ret
.kc_fail:
        mov eax, -1
        pop edx
        pop ecx
        ret

;-----------------------------------------------------------------------
; kmem_alloc - take one object from a cache
;   In : EAX = cache id          Out: EAX = zeroed object, or 0
;
; Runs inside the page-fault handler's callers (a task's area table is read on
; every demand fault), so it must not assume the new slab page is mapped: pmm
; pages above the boot identity map are reached through the identity-demand
; fault path like any other kernel access.
;-----------------------------------------------------------------------
kmem_alloc:
        pushad
        cmp eax, KMEM_CACHES
        jae .ka_fail
        mov [.ka_cache], eax
        cmp dword [kmem_obj_size + eax*4], 0
        je .ka_fail                     ; unregistered cache

        mov edi, [kmem_free_list + eax*4]
        cmp edi, KMEM_FREE_END
        jne .ka_pop
        call kmem_new_slab
        test eax, eax
        jz .ka_fail
        mov eax, [.ka_cache]
        mov edi, [kmem_free_list + eax*4]
.ka_pop:
        mov edx, [edi]                  ; next free object
        mov ecx, [.ka_cache]
        mov [kmem_free_list + ecx*4], edx

        ; Hand out a zeroed object: slots are recycled constantly.
        mov [.ka_obj], edi
        mov ecx, [kmem_obj_size + ecx*4]
        shr ecx, 2
        xor eax, eax
        mov edi, [.ka_obj]
        cld
        rep stosd

        mov eax, [.ka_obj]
        mov [esp + 28], eax
        popad
        ret
.ka_fail:
        mov dword [esp + 28], 0
        popad
        ret
.ka_cache: dd 0
.ka_obj:   dd 0

;-----------------------------------------------------------------------
; kmem_new_slab - take a page from the PMM and thread its objects
;   In : EAX = cache id          Out: EAX = 1 on success, 0 on OOM
;-----------------------------------------------------------------------
kmem_new_slab:
        pushad
        mov [.ns_cache], eax
        mov edi, eax
        mov edx, [kmem_per_slab + edi*4]
        mov [.ns_count], edx
        mov edx, [kmem_obj_size + edi*4]
        mov [.ns_size], edx

        call pmm_alloc_page
        test eax, eax
        jz .ns_fail
        mov [.ns_page], eax

        mov edi, eax                    ; current object
        mov ecx, [.ns_count]
        mov edx, [.ns_size]
        mov esi, edi                    ; previous object
.ns_link:
        add edi, edx                    ; next object
        dec ecx
        jz .ns_last
        mov [esi], edi
        mov esi, edi
        jmp .ns_link
.ns_last:
        mov dword [esi], KMEM_FREE_END

        mov eax, [.ns_cache]
        mov edx, [.ns_page]
        mov [kmem_free_list + eax*4], edx
        mov dword [esp + 28], 1
        popad
        ret
.ns_fail:
        mov dword [esp + 28], 0
        popad
        ret
.ns_cache: dd 0
.ns_page:  dd 0
.ns_count: dd 0
.ns_size:  dd 0

;-----------------------------------------------------------------------
; kmem_free - return an object to its cache
;   In : EAX = cache id, EBX = object (0 is ignored)
;-----------------------------------------------------------------------
kmem_free:
        push eax
        push ecx
        push edx
        test ebx, ebx
        jz .kf_done
        cmp eax, KMEM_CACHES
        jae .kf_done
        cmp dword [kmem_obj_size + eax*4], 0
        je .kf_done
        mov edx, [kmem_free_list + eax*4]
        mov [ebx], edx
        mov [kmem_free_list + eax*4], ebx
.kf_done:
        pop edx
        pop ecx
        pop eax
        ret
