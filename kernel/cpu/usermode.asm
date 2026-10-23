bits 32

section .data
global saved_kernel_esp
global saved_kernel_ebp

saved_kernel_esp dd 0
saved_kernel_ebp dd 0

section .text
global enter_usermode

enter_usermode:
    ; Preserve callee-saved registers so this behaves like a normal C call.
    push ebx
    push esi
    push edi

    ; [esp+12] = return addr, [esp+16] = User EIP, [esp+20] = User ESP
    mov eax, [esp+16]
    mov ebx, [esp+20]
    mov [saved_kernel_esp], esp
    mov [saved_kernel_ebp], ebp

    mov cx, 0x23
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    push dword 0x23
    push ebx
    push dword 0x202
    push dword 0x1B
    push eax
    iret