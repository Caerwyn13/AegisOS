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
- Paging and virtual memory
- Heap allocator (`kmalloc`/`kfree`)
- PIT timer with uptime tracking
- RTC driver for real date and time
- ATA disk driver for persistent storage
- AegisFS — a custom filesystem with persistent file storage
- ELF binary loader — load and execute ELF programs from disk
- Syscall interface (`int 0x80`) for userspace programs
- Interactive shell with command history and pagination

## Shell Commands

| Command | Description |
|---|---|
| `help` | Show available commands |
| `help -p <page>` | Show a specific page of help |
| `clear` | Clear the screen |
| `clear -c <colour>` | Clear with a background colour |
| `echo <text>` | Print text to the screen |
| `mem` | Show free memory in KB |
| `memmap` | Show the GRUB memory map |
| `uptime` | Show system uptime |
| `date` | Show current date and time |
| `hexdump <addr> [len]` | Dump memory in hex |
| `history` | Show command history |
| `heap` | Show heap statistics |
| `ls` | List files on disk |
| `cat <file>` | Read a file |
| `write <file> <content>` | Write to a file |
| `touch <file>` | Create an empty file |
| `rm <file>` | Delete a file |
| `exec <file>` | Execute an ELF binary |
| `about` | About AegisOS |
| `reboot` | Reboot the system |
| `shutdown` | Shutdown the system |

Available colours for `clear -c`: `black`, `blue`, `green`, `cyan`, `red`, `magenta`, `brown`, `grey`, `darkgrey`, `lightblue`, `lightgreen`, `lightcyan`, `lightred`, `lightmagenta`, `yellow`, `white`

## Syscall Interface

Userspace programs communicate with the kernel via `int 0x80`. Set `eax` to the syscall number and arguments in `ebx`, `ecx`, `edx`.

| Number | Name | Arguments | Description |
|---|---|---|---|
| 0 | `SYS_EXIT` | `ebx` = exit code | Exit the program |
| 1 | `SYS_PRINT` | `ebx` = string pointer | Print a string |
| 2 | `SYS_READ` | `ebx` = name, `ecx` = buf, `edx` = size ptr | Read a file |
| 3 | `SYS_WRITE` | `ebx` = name, `ecx` = buf, `edx` = size | Write a file |
| 4 | `SYS_OPEN` | `ebx` = name | Open/create a file |
| 5 | `SYS_CLOSE` | — | Close a file |
| 6 | `SYS_GETTIME` | `ebx` = rtc_time_t pointer | Get current time |

### Writing Userspace Programs

Programs must be compiled as static 32-bit ELF executables with no standard library:

```c
void _start() {
    const char *msg = "Hello from userspace!\n";

    // SYS_PRINT
    __asm__ volatile (
        "mov %0, %%ebx\n"
        "mov $1, %%eax\n"
        "int $0x80\n"
        : : "r"(msg) : "eax", "ebx"
    );

    // SYS_EXIT
    __asm__ volatile (
        "mov $0, %%ebx\n"
        "mov $0, %%eax\n"
        "int $0x80\n"
        : : : "eax", "ebx"
    );
}
```

Compile with:
```bash
gcc -m32 -ffreestanding -nostdlib -nodefaultlibs \
    -static -no-pie -fno-pic \
    -c hello.c -o hello.o

ld -m elf_i386 -nostdlib --no-dynamic-linker \
    -Ttext=0x400000 hello.o -o hello.elf
```

Write to disk using the included `diskwrite` tool:
```bash
gcc -o tools/diskwrite tools/diskwrite.c
./tools/diskwrite disk.img hello.elf
```

Then run in AegisOS:
```
exec hello.elf
```

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
│   │   ├── pic.c / pic.h
│   │   ├── pit.c / pit.h
│   │   └── syscall.c / syscall.h
│   ├── drivers/
│   │   ├── vga.c / vga.h
│   │   ├── keyboard.c / keyboard.h
│   │   ├── serial.c / serial.h
│   │   ├── ata.c / ata.h
│   │   └── rtc.c / rtc.h
│   ├── mm/
│   │   ├── pmm.c / pmm.h
│   │   ├── paging.c / paging.h
│   │   └── heap.c / heap.h
│   ├── fs/
│   │   ├── aegisfs.c / aegisfs.h
│   │   └── elf.c / elf.h
│   ├── lib/
│   │   └── string.c / string.h
│   └── shell/
│       └── shell.c / shell.h
├── include/
│   ├── types.h
│   ├── stdarg.h
│   └── multiboot.h
├── tools/
│   └── diskwrite.c
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

## Roadmap

- [ ] User mode (ring 3)
- [ ] Multitasking / process scheduler
- [ ] Expand AegisFS (directories, append, file permissions)
- [ ] Network driver

## License

MIT
