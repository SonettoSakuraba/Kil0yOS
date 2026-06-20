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
/*
 * --------------------------------------------------------------
 * Kernel Main Entry Point
 * 
 ***************************************************************
 * 明天我将如何进行这些计算？
 * 当bootloader把控制权交还给我,CS:IP指向这里,
 * 心中的电平,在保护模式的段界限里震荡,
 * 能否穿过GDT的描述符，传得更远更远?
 * 我把所有的寄存器压栈.
 * 如果明天的时钟中断不再到来，至少此刻,
 * 0x10 的代码段里,还有一个完整的上下文.
 * --------------------------------------------------------------
 */
void kernel_main() {
    vga_init();
    
    vga_set_color(vga_entry_color(COLOR_LIGHT_CYAN, COLOR_BLACK));
    vga_puts("Kil0yOS v1.0.4\n");
    vga_puts("==================================\n");
    vga_set_color(vga_entry_color(COLOR_WHITE, COLOR_BLACK));
    
    vga_puts("[GDT] Initializing\n");
    gdt_init();
    vga_puts("[GDT] OK\n");
    
    vga_puts("[IDT] Initializing\n");
    idt_init();
    vga_puts("[IDT] OK\n");
    
    vga_puts("[ISRs] Initializing\n");
    isr_init();
    vga_puts("[ISRs] OK\n");
    
    vga_puts("[PIC] Initializing\n");
    interrupts_init();
    vga_puts("[PIC] OK\n");
    
    vga_puts("[Memory] Initializing\n");
    memory_map_t map = {0};
    memory_init(&map, 1);
    vga_puts("[Memory] OK\n");
    
    vga_puts("[DeviceManager] Initializing\n");
    device_init();
    vga_puts("[DeviceManager] OK\n");
    
    vga_puts("[Filesystem] Initializing\n");
    fs_init();
    vga_puts("[Filesystem] OK\n");
    
    vga_puts("[Shell] Initializing\n");
    shell_init();
    vga_puts("[Shell] OK\n");
    
    vga_puts("[Keyboard] Initializing\n");
    keyboard_init();
    vga_puts("[Keyboard] OK\n");
    
    vga_puts("[Scheduler] Initializing\n");
    scheduler_init();
    vga_puts("[Scheduler] OK\n");
    
    vga_puts("[PIT] Initializing\n");
    pit_init(100);
    vga_puts("[PIT] OK\n");
    
    vga_puts("\nWelcome!\n");
    vga_puts("Type 'help' for available commands.\n\n");
    
    enable_interrupts();
    
    shell_run();
}