#include "pit.h"
#include "irq.h"
#include "serial.h"

#define PIT_CHANNEL0  0x40
#define PIT_COMMAND   0x43
#define PIT_BASE_FREQ 1193182

static volatile uint32_t ticks = 0;
static uint32_t frequency      = 0;

static void pit_handler(registers_t *regs) {
    (void)regs;
    ticks++;
}

void pit_init(uint32_t freq) {
    frequency   = freq;
    uint32_t divisor = PIT_BASE_FREQ / freq;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register(0, pit_handler);
}

uint32_t pit_ticks() {
    return ticks;
}

void pit_sleep(uint32_t ms) {
    uint32_t target = ticks + (frequency * ms / 1000);
    while (ticks < target);
}