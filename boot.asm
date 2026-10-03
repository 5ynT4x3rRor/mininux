bits 16
org 0x7c00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    mov [boot_drive], dl

load_kernel:
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 49
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

gdt_start:
    dq 0
    dw 0xffff, 0
    db 0, 0x9a, 0xcf, 0
    dw 0xffff, 0
    db 0, 0x92, 0xcf, 0
    dw 0xffff, 0
    db 0, 0x9a, 0x00, 0
    dw 0xffff, 0
    db 0, 0x92, 0x00, 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

bits 32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000
    call 0x10000

halt:
    cli
    hlt
    jmp halt

bits 16
boot_drive db 0
saved_esp dd 0

times 0x100 - ($ - $$) db 0

; Appelable depuis le noyau (0x7d00): ecrit via INT 13h ext. (AH=0x43)
; avec le DAP a 0x600; code retour BIOS dans l'octet 0x5fc.
bits 32
bios_write:
    pushad
    mov [saved_esp], esp
    jmp 0x18:rm_entry

bits 16
rm_entry:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr0
    and eax, 0xfffffffe
    mov cr0, eax
    jmp 0x0000:rm_start

rm_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    mov dl, [boot_drive]
    mov si, 0x600
    mov ah, 0x43
    xor al, al
    sti
    int 0x13
    cli
    jnc .done
    or ah, ah
    jnz .done
    mov ah, 0xff
.done:
    mov [0x5fc], ah
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:back32

bits 32
back32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [saved_esp]
    popad
    ret

bits 16

times 510 - ($ - $$) db 0
dw 0xaa55