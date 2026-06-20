#include "pit.h"
#include "io.h"
#include "interrupts.h"

#define PIT_COMMAND   0x43
#define PIT_CHANNEL0  0x40
#define PIT_BASE_FREQ 1193180

void pit_init(uint32_t frequency) {
    uint32_t divisor = PIT_BASE_FREQ / frequency;

    // Channel 0, lobyte/hibyte, mode 2 (rate generator), 16-bit binary
    outb(PIT_COMMAND, 0x36);

    // Send divisor (low byte then high byte)
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    // Enable IRQ 0 in PIC
    pic_enable_irq(0);
}