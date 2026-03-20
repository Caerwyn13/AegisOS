bits 32

section .text

extern isr_handler

global idt_flush
idt_flush:
	mov eax, [esp+4]
	lidt [eax]
	ret

; Macro for ISRs w/out error code
%macro ISR_NOERR 1
global isr%1
isr%1:
	cli
	push dword 0	; dummy err
	push dword %1	; interrupt num
	jmp isr_common
%endmacro

; Macro for ISRs w/out error code (CPU pushes automatically)
%macro ISR_ERR 1
global isr%1
isr%1:
	cli
	push dword %1
	jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8   ; double fault
ISR_NOERR 9
ISR_ERR   10  ; invalid TSS
ISR_ERR   11  ; segment not present
ISR_ERR   12  ; stack fault
ISR_ERR   13  ; general protection fault  <-- THIS ONE
ISR_ERR   14  ; page fault
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17  ; alignment check
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30  ; security exception
ISR_NOERR 31

isr_common:
	pusha
	mov ax, ds
	push eax
	mov ax, 0x10	; kernel data segment
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	push esp	; pointer to registers_t
	call isr_handler
	pop eax
	pop eax
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	popa
	add esp, 8	; Clean up int_no and err_code
	sti
	iret


;=======================================

%macro IRQ 2
global irq%1
irq%1:
	cli
	push dword 0
	push dword %2
	jmp irq_common
%endmacro

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

extern irq_handler
irq_common:
	pusha
	mov ax, ds
	push eax
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	push esp
	call irq_handler
	pop eax
	pop eax
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	popa
	add esp, 8
	sti
	iret


;=======================================

extern syscall_handler

global isr128
isr128:
	cli
	push dword 0
	push dword 128
	jmp syscall_common


extern process_exited
extern return_func
extern saved_kernel_esp

syscall_common:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call syscall_handler
    pop eax
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8

    ; check if process exited
    cmp dword [process_exited], 1
    je .process_exit
    sti
    iret

.process_exit:
    ; reset flag
    mov dword [process_exited], 0
    ; restore saved kernel stack
    mov esp, [saved_kernel_esp]
    sti
    ; call return function
    mov eax, [return_func]
    call eax
    hlt