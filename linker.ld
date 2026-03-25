ENTRY(_start)

SECTIONS {
    . = 1M;  /* LOAD AT 1MB */

    /* multiboot header */
    .multiboot : { *(.multiboot) }

    _kernel_start = .;

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }

    kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
    }
}