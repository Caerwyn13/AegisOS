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
- ACPI shutdown and reboot support
- Preemptive round-robin scheduling for kernel tasks
- QEMU e1000 networking with DHCP, DNS lookups, IPv4 ping, and TCP connection tests
- Interactive shell with a colour-coded prompt and output, editable command lines,
  arrow-key history, and paginated help

## Shell Commands

| Command | Description |
|---|---|
| `help` | Show available commands |
| `help -p <page>` | Show a specific page of help |
| `clear` | Clear the screen |
| `clear -c <colour>` | Clear with a background colour |
| `echo <text>` | Print text to the screen |
| `rps <choice>` | Play rock-paper-scissors (`rock`, `paper`, or `scissors`) against AegisOS |
| `net` | Show the network interface configuration |
| `ping <IPv4\|hostname>` | Send four ICMP echo requests to an IPv4 address or resolve and ping a hostname |
| `tcp <IPv4\|hostname> <port>` | Open a TCP connection, then attempt a clean close |
| `http <IPv4\|hostname> [path] [file]` | Fetch an HTTP path; optionally save its body to AegisFS |
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
| `edit <file>` | Create or edit a text file |
| `run <file>` | Run an Aegis language script |
| `touch <file>` | Create an empty file |
| `rm <file>` | Delete a file |
| `exec <file>` | Execute an ELF binary |
| `about` | About AegisOS |
| `reboot` | Reboot the system |
| `shutdown` | Shutdown the system |

Available colours for `clear -c`: `black`, `blue`, `green`, `cyan`, `red`, `magenta`, `brown`, `grey`, `darkgrey`, `lightblue`, `lightgreen`, `lightcyan`, `lightred`, `lightmagenta`, `yellow`, `white`

Use the left and right arrow keys to move within the command line. Backspace
removes the character before the cursor, Delete removes the character under
the cursor, and Home/End move to the start or end. Up/Down cycle through the
command history; pressing Down past the newest entry restores the line you were
typing before browsing history.

Use `edit <file>` to create or edit a text file in the built-in editor. Type
normally, use the arrow keys to move, and press Ctrl+S to save or Ctrl+Q to
quit. If there are unsaved changes, Ctrl+Q asks for confirmation; press it a
second time to discard them, or Ctrl+S to save.

Aegis's built-in scripting language supports variables, arithmetic, formatted
printing, conditionals, and `while` loops. Scripts run from files stored in
AegisFS; see the [language guide](./language/README.md) for syntax and examples.

## Rock-Paper-Scissors

Play one round by choosing `rock`, `paper`, or `scissors`:

```text
rps rock
```

AegisOS shows both choices and the result. Run the command again to play another round.

## Networking

The network driver supports QEMU's emulated Intel e1000 adapter. At startup,
the network manager obtains the guest IPv4 address, netmask, gateway, and DNS
server using DHCP. Run `net` to display the configuration, or use `ping` with
an IPv4 address or hostname:

```text
ping 10.0.2.2
ping example.com
```

Hostname lookups use the DNS server provided by DHCP and support IPv4 address
(A) records. If DHCP does not provide a DNS server, numeric-address pings still
work, but hostname lookups cannot. `ping` sends four ICMP echo requests.
The `tcp` command is a connection test only; it does not send or receive
application data. For example, `tcp example.com 80` attempts a TCP handshake
and graceful close.
The `http` command performs a plain HTTP/1.0 GET request on port 80. An
optional absolute path selects a resource; if omitted, `/` is requested.
Response headers are parsed to show the HTTP status, and up to 1023 body bytes
are displayed. Add a filename to save a successful response body to AegisFS:

```text
http example.com
http example.com /robots.txt
http example.com / saved.html
```

Downloads are limited to the filesystem's 32 KiB maximum file size, require a
2xx HTTP status, decode chunked transfer responses, and do not overwrite an
existing file. Use `cat saved.html` to inspect a saved response, or remove it
with `rm saved.html` before downloading to that name again. HTTPS is not
supported.

`make run` enables the QEMU e1000 adapter and user-mode network. When launching
QEMU manually, include:

```bash
-netdev user,id=net0 -device e1000,netdev=net0
```

Only QEMU's emulated e1000 adapter with user-mode networking is supported;
physical network cards and VirtualBox networking are not supported.

## Syscall Interface

Userspace programs communicate with the kernel via `int 0x80`. Set `eax` to the syscall number and arguments in `ebx`, `ecx`, `edx`.

| Number | Name | Arguments | Description |
|---|---|---|---|
| 0 | `SYS_EXIT` | `ebx` = exit code | Exit the program |
| 1 | `SYS_PRINT` | `ebx` = string pointer | Print a NUL-terminated string (up to 4095 characters) |
| 2 | `SYS_READ` | `ebx` = name, `ecx` = buffer, `edx` = size pointer | Read a file; the buffer must fit the file |
| 3 | `SYS_WRITE` | `ebx` = name, `ecx` = buffer, `edx` = byte count | Write up to 32 KiB to a file |
| 4 | `SYS_OPEN` | `ebx` = name | Create a file if it does not exist |
| 5 | `SYS_CLOSE` | — | No-op; file handles are not implemented |
| 6 | `SYS_GETTIME` | `ebx` = `rtc_time_t` pointer | Get current time |

Except for `SYS_EXIT`, syscalls return `0` in `eax` on success and `-1` on
failure. User pointers must refer to mapped user memory with the required
permissions; invalid pointers are rejected. File names are limited to 31
characters, and reads and writes are limited to AegisFS's 32 KiB maximum file
size.

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

Create the disk image if it does not already exist, make the included
`diskwrite` tool executable, and write `hello.elf` to the image. Stop QEMU
before writing to the image:
```bash
make disk.img
chmod +x tools/diskwrite
./tools/diskwrite disk.img hello.elf
```

Start AegisOS with `make run`, then run the program in the shell:
```
exec hello.elf
```

## Project Structure

```
AegisOS/
├── boot/
│   └── grub.cfg
├── drivers/
│   ├── ata.c / ata.h
│   ├── e1000.c / e1000.h
│   ├── keyboard.c / keyboard.h
│   ├── ports.c / ports.h
│   ├── rtc.c / rtc.h
│   ├── serial.c / serial.h
│   └── vga.c / vga.h
├── kernel/
│   ├── boot.asm
│   ├── kernel.c
│   ├── acpi/
│   │   └── acpi.c / acpi.h
│   ├── cpu/
│   │   ├── gdt.c / gdt.h
│   │   ├── gdt_flush.asm
│   │   ├── idt.c / idt.h
│   │   ├── isr.c / isr.h
│   │   ├── isr.asm
│   │   ├── irq.c / irq.h
│   │   ├── pic.c / pic.h
│   │   ├── pit.c / pit.h
│   │   ├── syscall.c
│   │   ├── task.c / task.h
│   │   └── usermode.asm / usermode.h
│   ├── fs/
│   │   ├── aegisfs.c / aegisfs.h
│   │   └── elf.c / elf.h
│   ├── lib/
│   │   └── string.c / string.h
│   ├── mm/
│   │   ├── pmm.c / pmm.h
│   │   ├── paging.c / paging.h
│   │   └── heap.c / heap.h
│   ├── net/
│   │   └── net.c / net.h
│   └── shell/
│       ├── editor.c / editor.h
│       ├── script.c / script.h
│       └── shell.c / shell.h
├── include/
│   ├── types.h
│   ├── stdarg.h
│   ├── multiboot.h
│   ├── random.c / random.h
│   └── syscall.h
├── language/
│   └── README.md
├── tools/
│   └── diskwrite
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

> **Warning:** AegisOS has been tested in QEMU, but has not been tested in VirtualBox. It may not boot or work correctly there.

1. Open VirtualBox and create a new VM:
   - **Type**: Other
   - **Version**: Other/Unknown (32-bit)
   - **Memory**: 32MB or more
2. Under **Storage**, attach the `AegisOS.iso` as an optical drive
3. Under **Display**, ensure VGA is selected
4. Boot the VM — AegisOS should load automatically

### Running on Real Hardware

> **Warning:** AegisOS has not been tested on real hardware. Booting or using it on a physical machine may fail or behave unexpectedly.

You can write the ISO to a USB drive and boot from it on real hardware:

**On Linux/macOS:**
```bash
sudo dd if=AegisOS.iso of=/dev/sdX bs=4M status=progress
```
Replace `/dev/sdX` with your USB drive (be careful to choose the correct drive).

**On Windows:**
Use [Rufus](https://rufus.ie) to write the ISO to a USB drive in DD mode.

## Roadmap

- [x] User mode (ring 3)
- [x] Initial kernel-task scheduler
- [ ] Multitasking for independent user processes
- [ ] Expand AegisFS (directories, append, file permissions)
- [ ] Network driver

## License

MIT
