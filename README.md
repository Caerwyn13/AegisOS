# AegisOS

A simple x86 operating system written in C and NASM assembly.

![License](https://img.shields.io/badge/license-MIT-blue)
![Architecture](https://img.shields.io/badge/arch-x86-green)
![Language](https://img.shields.io/badge/language-C%20%2F%20NASM-orange)

## Features

- Multiboot compliant bootloader (GRUB)
- Global Descriptor Table (GDT)
- Interrupt Descriptor Table (IDT) with exception handling
- Programmable Interrupt Controller (PIC) with IRQ support
- PS/2 Keyboard driver with UK layout, shift, caps lock and arrow keys
- VGA text mode driver with colour support
- Physical Memory Manager (PMM) using a bitmap allocator
- Interactive shell with command history

## Shell Commands

| Command | Description |
|---|---|
| `help` | Show available commands |
| `clear` | Clear the screen |
| `clear -c <colour>` | Clear with a background colour |
| `echo <text>` | Print text to the screen |
| `mem` | Show free memory in KB |
| `memmap` | Show the GRUB memory map |
| `history` | Show command history |
| `about` | About AegisOS |
| `reboot` | Reboot the system |
| `shutdown` | Shutdown the system |

Available colours for `clear -c`: `black`, `blue`, `green`, `cyan`, `red`, `magenta`, `brown`, `grey`, `darkgrey`, `lightblue`, `lightgreen`, `lightcyan`, `lightred`, `lightmagenta`, `yellow`, `white`

## Project Structure

```
AegisOS/
├── boot/
│   └── grub.cfg
├── kernel/
│   ├── boot.asm
│   ├── kernel.c
│   ├── cpu/
│   │   ├── gdt.c / gdt.h
│   │   ├── gdt_flush.asm
│   │   ├── idt.c / idt.h
│   │   ├── isr.c / isr.h
│   │   ├── isr.asm
│   │   ├── irq.c / irq.h
│   │   └── pic.c / pic.h
│   ├── drivers/
│   │   ├── vga.c / vga.h
│   │   ├── keyboard.c / keyboard.h
│   │   └── serial.c / serial.h
│   ├── mm/
│   │   └── pmm.c / pmm.h
│   ├── lib/
│   │   └── string.c / string.h
│   └── shell/
│       └── shell.c / shell.h
├── include/
│   ├── types.h
│   └── multiboot.h
├── linker.ld
└── Makefile
```

## Building

### Prerequisites

- `nasm`
- `gcc`
- `ld`
- `grub`
- `xorriso`
- `mtools`
- `qemu-system-x86`

On Arch Linux:
```bash
sudo pacman -S nasm gcc make grub xorriso mtools qemu-system-x86
```

On Ubuntu/Debian:
```bash
sudo apt install nasm gcc make grub-pc-bin grub-common xorriso mtools qemu-system-x86
```

### Build and Run

```bash
# Build the ISO
make

# Run in QEMU
make run

# Clean build files
make clean
```

### QEMU Setup

AegisOS runs with a VNC display and QEMU monitor over telnet.

**1. Run the ISO in QEMU:**
```bash
make run
```

**2. Connect a VNC client** to `localhost:5900` — this is your display and keyboard input. [RealVNC Viewer](https://www.realvnc.com/en/connect/download/viewer/) is recommended.

**3. Optionally connect to the QEMU monitor** via telnet for debugging:
```bash
telnet localhost 4444
```

### Running the ISO Manually

If you want to run the produced ISO directly without `make run`:

```bash
qemu-system-i386 -cdrom AegisOS.iso -serial mon:stdio -monitor telnet:localhost:4444,server,nowait
```

### Running on VirtualBox

1. Open VirtualBox and create a new VM:
   - **Type**: Other
   - **Version**: Other/Unknown (32-bit)
   - **Memory**: 32MB or more
2. Under **Storage**, attach the `AegisOS.iso` as an optical drive
3. Under **Display**, ensure VGA is selected
4. Boot the VM — AegisOS should load automatically

### Running on Real Hardware

You can write the ISO to a USB drive and boot from it on real hardware:

**On Linux/macOS:**
```bash
sudo dd if=AegisOS.iso of=/dev/sdX bs=4M status=progress
```
Replace `/dev/sdX` with your USB drive (be careful to choose the correct drive).

**On Windows:**
Use [Rufus](https://rufus.ie) to write the ISO to a USB drive in DD mode.

## License

MIT
