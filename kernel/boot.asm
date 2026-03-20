; kernel/boot.asm
bits 32 

MAGIC equ 0x1BADB002
FLAGS equ 0x2					; bit 1 = request memory map from GRUB
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
	dd MAGIC
	dd FLAGS
	dd CHECKSUM

global stack_top

section .bss
align 16
stack_bottom:
	resb 16384
stack_top:

section .text
global _start
extern kernel_main

_start:
	mov esp, stack_top
	push ebx			; Multiboot info ptr passed by GRUB in ebx
	call kernel_main
	cli
.hang:
	hlt
	jmp .hang
