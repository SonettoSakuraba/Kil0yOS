#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "interrupts.h"
#include "memory.h"
#include "vga.h"
#include "keyboard.h"
#include "shell.h"
#include "fs.h"
#include "device.h"
#include "pit.h"
#include "scheduler.h"

/* Test tasks for round-robin demonstration */
static void test_task1() {
    while (1) {
        vga_set_color(vga_entry_color(COLOR_LIGHT_GREEN, COLOR_BLACK));
        vga_puts("[Task 1 running]\n");
        vga_set_color(vga_entry_color(COLOR_WHITE, COLOR_BLACK));
        for (volatile uint32_t i = 0; i < 3000000; i++);
    }
}

static void test_task2() {
    while (1) {
        vga_set_color(vga_entry_color(COLOR_LIGHT_RED, COLOR_BLACK));
        vga_puts("[Task 2 running]\n");
        vga_set_color(vga_entry_color(COLOR_WHITE, COLOR_BLACK));
        for (volatile uint32_t i = 0; i < 3000000; i++);
    }
}

void kernel_main() {
    vga_init();
    
    vga_set_color(vga_entry_color(COLOR_LIGHT_CYAN, COLOR_BLACK));
    vga_puts("Kil0yOS v1.0.4 - 32-bit Microkernel\n");
    vga_puts("=================================\n");
    vga_set_color(vga_entry_color(COLOR_WHITE, COLOR_BLACK));
    
    vga_puts("[1/9] Initializing GDT...\n");
    gdt_init();
    vga_puts("[OK] GDT initialized\n");
    
    vga_puts("[2/9] Initializing IDT...\n");
    idt_init();
    vga_puts("[OK] IDT initialized\n");
    
    vga_puts("[3/9] Initializing ISRs...\n");
    isr_init();
    vga_puts("[OK] ISRs initialized\n");
    
    vga_puts("[4/9] Initializing PIC...\n");
    interrupts_init();
    vga_puts("[OK] PIC initialized\n");
    
    vga_puts("[5/9] Initializing Memory...\n");
    memory_map_t map = {0};
    memory_init(&map, 1);
    vga_puts("[OK] Memory initialized\n");
    
    vga_puts("[6/9] Initializing Device Manager...\n");
    device_init();
    vga_puts("[OK] Device Manager initialized\n");
    
    vga_puts("[7/9] Initializing Filesystem...\n");
    fs_init();
    vga_puts("[OK] Filesystem initialized\n");
    
    vga_puts("[8/9] Initializing Shell...\n");
    shell_init();
    vga_puts("[OK] Shell initialized\n");
    
    vga_puts("[9/9] Initializing Keyboard...\n");
    keyboard_init();
    vga_puts("[OK] Keyboard initialized\n");
    
    vga_puts("\nWelcome to Kil0yOS!\n");
    vga_puts("Type 'help' for available commands.\n\n");
    
    /* [10] Initialize scheduler and create demo tasks */
    scheduler_init();
    task_create(test_task1, "test1");
    task_create(test_task2, "test2");
    
    /* [11] Start PIT timer at 100 Hz – enables preemptive multi-tasking */
    pit_init(100);
    
    enable_interrupts();
    
    shell_run();
}