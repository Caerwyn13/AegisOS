#ifndef PIC_H
#define PIC_H

#include "types.h"

#define PIC1		0x20
#define PIC2		0xA0
#define PIC1_COMMAND	PIC1
#define PIC1_DATA	(PIC1+1)
#define PIC2_COMMAND    PIC2
#define PIC2_DATA	(PIC2+1)
#define PIC2_EOI	0x20

void pic_init();
void pic_eoi(unsigned char irq);
void pic_unmask(uint8_t irq);
void pic_mask(uint8_t irq);

#endif // PIC_H
