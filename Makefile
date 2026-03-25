# Makefile
ASM := nasm
CD  := gcc
LD  := ld

ASMFLAGS := -f elf32
CIMPS    := -Iinclude -Idrivers -Iports -Ikernel/cpu -Ikernel/mm -Ikernel/shell -Ikernel/lib -Ikernel/fs -Ikernel/games/snake
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

disk.img:
	@if [ ! -f disk.img ]; then \
		dd if=/dev/zero of=disk.img bs=1M count=64; \
	fi

run: $(ISO) disk.img
	qemu-system-i386 -cdrom $(ISO) -serial mon:stdio \
	-monitor telnet:localhost:4444,server,nowait \
	-drive file=disk.img,format=raw,index=0,media=disk,cache=none \
	-m 256M

clean:
	rm -rf $(OBJ_DIR) iso $(KERNEL) $(ISO)

# separate target to wipe the disk
clean-disk:
	rm -f disk.img