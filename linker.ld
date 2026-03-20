ENTRY(_start)

SECTIONS {
	. = 1M;		/* LOAD AT 1MB */
	
	.multiboot : { *(.multiboot) }
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