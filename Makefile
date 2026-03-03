# Makefile
ASM := nasm
CD  := gcc
LD  := ld

ASMFLAGS := -f elf32
CIMPS    := -Iinclude -Icpu -Idrivers -Iports -Ikernel/mm -Ikernel/shell -Ikernel/lib
CFLAGS   := -m32 -ffreestanding -fno-stack-protector -nostdlib -nodefaultlibs -Wall -Wextra ${CIMPS}
LDFLAGS  := -m elf_i386 -T linker.ld

SRC_DIR := kernel
OBJ_DIR := obj

C_SRCS   := $(shell find . -name "*.c")
ASM_SRCS := $(shell find . -name "*.asm")

C_OBJS   := $(patsubst %.c,   $(OBJ_DIR)/%.o,     $(C_SRCS))
ASM_OBJS := $(patsubst %.asm, $(OBJ_DIR)/%.asm.o, $(ASM_SRCS))

ALL_OBJS := $(ASM_OBJS) $(C_OBJS)

ISO    := AegisOS.iso
KERNEL := kernel.bin

.PHONY: all clean run

all: $(ISO)

$(KERNEL): $(ALL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.asm.o: %.asm | $(OBJ_DIR)
	mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(ISO): $(KERNEL)
	mkdir -p iso/boot/grub
	cp $(KERNEL) iso/boot/kernel.bin
	cp boot/grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -serial mon:stdio -monitor telnet:localhost:4444,server,nowait

clean:
	rm -rf $(OBJ_DIR) iso $(KERNEL) $(ISO)
