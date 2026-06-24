#include "net/e1000.h"
#include "net/net.h"
#include "net/endian.h"
#include "drivers/vga.h"
#include "drivers/pci.h"
#include "drivers/io.h"
#include "core/interrupts.h"
#include "core/isr.h"
#include "mm/memory.h"
#include "lib/string.h"
#include "lib/stdlib.h"

#define ARP_CACHE_SIZE 16

typedef struct arp_entry {
    uint32_t ip;
    uint8_t mac[6];
    int valid;
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

static uint16_t e1000_io_base = 0;
static e1000_tx_desc_t* e1000_tx_descs = NULL;
static uint8_t* e1000_tx_buffers = NULL;
static e1000_rx_desc_t* e1000_rx_descs = NULL;
static uint8_t* e1000_rx_buffers = NULL;
static int e1000_current_tx = 0;
static int e1000_current_rx = 0;

uint8_t e1000_mac[6];

static inline void e1000_write_reg32(uint32_t offset, uint32_t value) {
    outd(e1000_io_base + offset, value);
}

static inline uint32_t e1000_read_reg32(uint32_t offset) {
    return ind(e1000_io_base + offset);
}

static void e1000_read_mac() {
    uint32_t mac_low = e1000_read_reg32(0x5400);
    uint32_t mac_high = e1000_read_reg32(0x5404);
    
    e1000_mac[0] = (mac_low >> 0) & 0xFF;
    e1000_mac[1] = (mac_low >> 8) & 0xFF;
    e1000_mac[2] = (mac_low >> 16) & 0xFF;
    e1000_mac[3] = (mac_low >> 24) & 0xFF;
    e1000_mac[4] = (mac_high >> 0) & 0xFF;
    e1000_mac[5] = (mac_high >> 8) & 0xFF;
}

static void e1000_reset() {
    e1000_write_reg32(E1000_REG_CTRL, E1000_CTRL_RST);
    while (e1000_read_reg32(E1000_REG_CTRL) & E1000_CTRL_RST) {
        __asm__ volatile("nop");
    }
}

static void e1000_setup_tx() {
    e1000_tx_descs = kmalloc(E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    e1000_tx_buffers = kmalloc(E1000_TX_DESC_COUNT * E1000_BUFFER_SIZE);
    
    memset(e1000_tx_descs, 0, E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    
    for (int i = 0; i < E1000_TX_DESC_COUNT; i++) {
        e1000_tx_descs[i].addr = (uint64_t)(e1000_tx_buffers + i * E1000_BUFFER_SIZE);
        e1000_tx_descs[i].status = E1000_TXD_STAT_DD;
    }
    
    e1000_write_reg32(E1000_REG_TDLEN, E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    e1000_write_reg32(E1000_REG_TDH, 0);
    e1000_write_reg32(E1000_REG_TDT, 0);
    
    e1000_write_reg32(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT | E1000_TCTL_COLD);
}

static void e1000_setup_rx() {
    e1000_rx_descs = kmalloc(E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    e1000_rx_buffers = kmalloc(E1000_RX_DESC_COUNT * E1000_BUFFER_SIZE);
    
    memset(e1000_rx_descs, 0, E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    
    for (int i = 0; i < E1000_RX_DESC_COUNT; i++) {
        e1000_rx_descs[i].addr = (uint64_t)(e1000_rx_buffers + i * E1000_BUFFER_SIZE);
    }
    
    e1000_write_reg32(E1000_REG_RDLEN, E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    e1000_write_reg32(E1000_REG_RDH, 0);
    e1000_write_reg32(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1);
    
    e1000_write_reg32(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048);
}

static void e1000_enable_interrupts() {
    e1000_write_reg32(E1000_REG_IMS, E1000_IMS_RXT0 | E1000_IMS_TXDW);
}

static void e1000_irq_handler(interrupt_frame_t* frame) {
    (void)frame;
    uint32_t icr = e1000_read_reg32(E1000_REG_ICR);
    
    if (icr & E1000_IMS_RXT0) {
        e1000_receive();
    }
    
    if (icr & E1000_IMS_TXDW) {
    }
    
    pic_send_eoi(9);
}

uint8_t* e1000_arp_cache_lookup(uint32_t ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            return arp_cache[i].mac;
        }
    }
    return NULL;
}

void e1000_arp_cache_update(uint32_t ip, uint8_t* mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = 1;
            return;
        }
    }
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    
    memcpy(arp_cache[0].mac, mac, 6);
    arp_cache[0].ip = ip;
}

int e1000_init(uint16_t io_base) {
    e1000_io_base = io_base;
    
    vga_puts("[E1000] Initializing...\n");
    
    e1000_reset();
    e1000_read_mac();
    
    vga_puts("[E1000] MAC: ");
    for (int i = 0; i < 6; i++) {
        char buf[3];
        utoa(e1000_mac[i], buf, 16, sizeof(buf));
        vga_puts(buf);
        if (i < 5) vga_puts(":");
    }
    vga_puts("\n");
    
    e1000_setup_tx();
    e1000_setup_rx();
    e1000_enable_interrupts();
    
    register_irq_handler(9, e1000_irq_handler);
    pic_enable_irq(9);
    
    vga_puts("[E1000] OK\n");
    
    return 0;
}

int e1000_send(uint8_t* data, size_t len) {
    if (e1000_io_base == 0) return -1;
    
    if (len > E1000_BUFFER_SIZE) return -1;
    
    while (!(e1000_tx_descs[e1000_current_tx].status & E1000_TXD_STAT_DD)) {
        __asm__ volatile("nop");
    }
    
    uint8_t* tx_buf = e1000_tx_buffers + e1000_current_tx * E1000_BUFFER_SIZE;
    memcpy(tx_buf, data, len);
    
    e1000_tx_descs[e1000_current_tx].len = len;
    e1000_tx_descs[e1000_current_tx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
    e1000_tx_descs[e1000_current_tx].status = 0;
    
    e1000_current_tx = (e1000_current_tx + 1) % E1000_TX_DESC_COUNT;
    
    e1000_write_reg32(E1000_REG_TDT, e1000_current_tx);
    
    return 0;
}

void e1000_receive() {
    while ((e1000_rx_descs[e1000_current_rx].status & E1000_RXD_STAT_DD)) {
        uint16_t len = e1000_rx_descs[e1000_current_rx].len;
        uint8_t* buf = e1000_rx_buffers + e1000_current_rx * E1000_BUFFER_SIZE;
        
        if (len > 0 && len <= E1000_BUFFER_SIZE) {
            net_handle_packet(buf, len);
        }
        
        e1000_rx_descs[e1000_current_rx].status = 0;
        e1000_current_rx = (e1000_current_rx + 1) % E1000_RX_DESC_COUNT;
        e1000_write_reg32(E1000_REG_RDT, e1000_current_rx);
    }
}
