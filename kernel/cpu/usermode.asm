bits 32

section .data
global saved_kernel_esp
global saved_kernel_ebp
global saved_kernel_eip        ; ← ADD THIS

saved_kernel_esp dd 0
saved_kernel_ebp dd 0
saved_kernel_eip dd 0          ; ← ADD THIS

section .text
global enter_usermode

enter_usermode:
    ; [esp] = return addr, [esp+4] = User EIP, [esp+8] = User ESP
    mov ecx, [esp]             ; Save return address
    mov eax, [esp+4]           ; User EIP
    mov ebx, [esp+8]           ; User ESP
    
    mov [saved_kernel_esp], esp
    mov [saved_kernel_ebp], ebp
    mov [saved_kernel_eip], ecx    ; ← Save return address!

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