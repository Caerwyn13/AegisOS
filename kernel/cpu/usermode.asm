bits 32

section .data
global saved_kernel_esp
global saved_kernel_eip
global return_func

saved_kernel_esp dd 0
saved_kernel_eip dd 0
return_func      dd 0

section .text
global enter_usermode
global return_to_kernel
global save_esp

save_esp:
    mov [saved_kernel_esp], esp
    ret

enter_usermode:
    mov eax, [esp]
    mov [saved_kernel_eip], eax
    lea eax, [esp+12]
    mov [saved_kernel_esp], eax

    mov eax, [esp+4]
    mov ebx, [esp+8]

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

return_to_kernel:
    mov esp, [saved_kernel_esp]
    sti
    mov eax, [return_func]
    call eax
    hlt