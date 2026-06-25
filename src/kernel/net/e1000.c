#include "net/e1000.h"
#include "net/net.h"
#include "net/endian.h"
#include "drivers/vga.h"
#include "drivers/pci.h"
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

static volatile uint32_t* e1000_io_base = NULL;
static e1000_tx_desc_t* e1000_tx_descs = NULL;
static uint8_t* e1000_tx_buffers = NULL;
static e1000_rx_desc_t* e1000_rx_descs = NULL;
static uint8_t* e1000_rx_buffers = NULL;
static int e1000_current_tx = 0;
static int e1000_current_rx = 0;

uint8_t e1000_mac[6];

#define E1000_REG_MDIC     0x00020
#define E1000_MDIC_OP_READ  0x00000001
#define E1000_MDIC_OP_WRITE 0x00000000
#define E1000_MDIC_PHY_SHIFT  21
#define E1000_MDIC_REG_SHIFT  16
#define E1000_MDIC_READY      0x00000001

static inline void e1000_write_reg32(uint32_t offset, uint32_t value) {
    e1000_io_base[offset / 4] = value;
}

static inline uint32_t e1000_read_reg32(uint32_t offset) {
    return e1000_io_base[offset / 4];
}

static void e1000_write_reg_array(uint32_t base, uint32_t index, uint32_t value) {
    e1000_io_base[(base + index) / 4] = value;
}

static uint32_t e1000_read_reg_array(uint32_t base, uint32_t index) {
    return e1000_io_base[(base + index) / 4];
}

static void e1000_rar_set(uint8_t* addr, uint32_t index) {
    uint32_t rar_low, rar_high;
    
    rar_low = ((uint32_t)addr[0] | ((uint32_t)addr[1] << 8) |
               ((uint32_t)addr[2] << 16) | ((uint32_t)addr[3] << 24));
    rar_high = ((uint32_t)addr[4] | ((uint32_t)addr[5] << 8));
    rar_high |= E1000_RAH_AV;
    
    e1000_write_reg_array(0x5400, index << 1, rar_low);
    e1000_write_reg_array(0x5400, (index << 1) + 1, rar_high);
}

static void e1000_read_mac() {
    uint32_t mac_low = e1000_read_reg_array(0x5400, 0);
    uint32_t mac_high = e1000_read_reg_array(0x5400, 1);
    
    e1000_mac[0] = (mac_low >> 0) & 0xFF;
    e1000_mac[1] = (mac_low >> 8) & 0xFF;
    e1000_mac[2] = (mac_low >> 16) & 0xFF;
    e1000_mac[3] = (mac_low >> 24) & 0xFF;
    e1000_mac[4] = (mac_high >> 0) & 0xFF;
    e1000_mac[5] = (mac_high >> 8) & 0xFF;
}

static uint16_t e1000_read_phy_reg(uint16_t reg) {
    uint32_t mdic;
    
    mdic = ((reg << E1000_MDIC_REG_SHIFT) |
            (1 << E1000_MDIC_PHY_SHIFT) |
            E1000_MDIC_OP_READ);
    
    e1000_write_reg32(E1000_REG_MDIC, mdic);
    
    for (int i = 0; i < 64; i++) {
        mdic = e1000_read_reg32(E1000_REG_MDIC);
        if (mdic & E1000_MDIC_READY)
            break;
    }
    
    return (uint16_t)mdic;
}

static void e1000_write_phy_reg(uint16_t reg, uint16_t value) {
    uint32_t mdic;
    
    mdic = ((reg << E1000_MDIC_REG_SHIFT) |
            (1 << E1000_MDIC_PHY_SHIFT) |
            E1000_MDIC_OP_WRITE |
            (value & 0xFFFF));
    
    e1000_write_reg32(E1000_REG_MDIC, mdic);
    
    for (int i = 0; i < 64; i++) {
        mdic = e1000_read_reg32(E1000_REG_MDIC);
        if (mdic & E1000_MDIC_READY)
            break;
    }
}

static void e1000_setup_phy() {
    uint16_t phy_ctrl = e1000_read_phy_reg(0x00);
    vga_puts("[E1000] PHY Ctrl before: 0x");
    char buf[5];
    utoa(phy_ctrl, buf, 16, sizeof(buf));
    vga_puts(buf);
    vga_puts("\n");
    
    phy_ctrl |= 0x1200;
    e1000_write_phy_reg(0x00, phy_ctrl);
    
    phy_ctrl = e1000_read_phy_reg(0x00);
    vga_puts("[E1000] PHY Ctrl after: 0x");
    utoa(phy_ctrl, buf, 16, sizeof(buf));
    vga_puts(buf);
    vga_puts("\n");
    
    for (int i = 0; i < 1000000; i++) {
        uint16_t phy_status = e1000_read_phy_reg(0x01);
        if (phy_status & 0x0004) {
            vga_puts("[E1000] Link detected!\n");
            break;
        }
        __asm__ volatile("nop");
    }
    
    uint16_t phy_status = e1000_read_phy_reg(0x01);
    vga_puts("[E1000] PHY Status: 0x");
    utoa(phy_status, buf, 16, sizeof(buf));
    vga_puts(buf);
    vga_puts("\n");
}

static void e1000_reset() {
    e1000_write_reg32(0x0000, E1000_CTRL_RST);
    for (int i = 0; i < 100000; i++) {
        if (!(e1000_read_reg32(0x0000) & E1000_CTRL_RST)) {
            break;
        }
        __asm__ volatile("nop");
    }
}

static void e1000_setup_tx() {
    e1000_tx_descs = kmalloc(E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    e1000_tx_buffers = kmalloc(E1000_TX_DESC_COUNT * E1000_BUFFER_SIZE);
    
    memset(e1000_tx_descs, 0, E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    
    for (int i = 0; i < E1000_TX_DESC_COUNT; i++) {
        e1000_tx_descs[i].addr = (uint32_t)(e1000_tx_buffers + i * E1000_BUFFER_SIZE);
        e1000_tx_descs[i].status = E1000_TXD_STAT_DD;
    }
    
    uint32_t tx_addr = (uint32_t)e1000_tx_descs;
    e1000_write_reg32(0x3800, tx_addr);
    e1000_write_reg32(0x3804, 0);
    e1000_write_reg32(0x3808, E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    e1000_write_reg32(0x3810, 0);
    e1000_write_reg32(0x3818, 0);
    
    e1000_write_reg32(0x0400, E1000_TCTL_EN | E1000_TCTL_PSP | (0x10 << 4) | (0x40 << 12));
}

static void e1000_setup_rx() {
    e1000_rx_descs = kmalloc(E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    e1000_rx_buffers = kmalloc(E1000_RX_DESC_COUNT * E1000_BUFFER_SIZE);
    
    memset(e1000_rx_descs, 0, E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    
    for (int i = 0; i < E1000_RX_DESC_COUNT; i++) {
        e1000_rx_descs[i].addr = (uint32_t)(e1000_rx_buffers + i * E1000_BUFFER_SIZE);
        e1000_rx_descs[i].status = 0;
    }
    
    uint32_t rx_addr = (uint32_t)e1000_rx_descs;
    e1000_write_reg32(0x2800, rx_addr);
    e1000_write_reg32(0x2804, 0);
    e1000_write_reg32(0x2808, E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    e1000_write_reg32(0x280C, 0);
    e1000_write_reg32(0x2810, 0);
    
    e1000_write_reg32(0x28A8, 0);
    e1000_write_reg32(0x28B0, E1000_RX_DESC_COUNT - 1);
    
    for (int i = 0; i < 128; i++) {
        e1000_write_reg_array(0x5200, i, 0);
    }
    
    e1000_write_reg32(0x0100, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048 | E1000_RCTL_SECRC | E1000_RCTL_UPE | E1000_RCTL_MPE);
}

static void e1000_enable_interrupts() {
    e1000_write_reg32(0x00D0, 0x00000000);
    e1000_write_reg32(0x00D4, E1000_IMS_RXT0 | E1000_IMS_TXDW);
    
    uint32_t ims = e1000_read_reg32(0x00D4);
    vga_puts("[E1000] IMS: 0x");
    char buf[9];
    utoa(ims, buf, 16, sizeof(buf));
    vga_puts(buf);
    vga_puts("\n");
}

static void e1000_irq_handler(interrupt_frame_t* frame) {
    (void)frame;
    uint32_t icr = e1000_read_reg32(0x00C0);
    
    vga_puts("[E1000] IRQ! ICR: 0x");
    char buf[9];
    utoa(icr, buf, 16, sizeof(buf));
    vga_puts(buf);
    vga_puts("\n");
    
    if (icr & E1000_IMS_RXT0) {
        vga_puts("[E1000] RX interrupt\n");
        e1000_receive();
    }
    
    if (icr & E1000_IMS_TXDW) {
        vga_puts("[E1000] TX done\n");
    }
    
    e1000_write_reg32(0x00C0, icr);
    pic_send_eoi(9);
}

int e1000_init(uint32_t io_base, uint16_t bus, uint16_t device, uint16_t func) {
    e1000_io_base = (volatile uint32_t*)io_base;
    
    vga_puts("[E1000] Initializing...\n");
    
    uint16_t cmd = pci_read_word(bus, device, func, PCI_COMMAND_OFFSET);
    cmd |= 0x0003;
    pci_write_word(bus, device, func, PCI_COMMAND_OFFSET, cmd);
    
    e1000_reset();
    
    e1000_read_mac();
    e1000_rar_set(e1000_mac, 0);
    
    vga_puts("[E1000] MAC: ");
    for (int i = 0; i < 6; i++) {
        char buf[3];
        utoa(e1000_mac[i], buf, 16, sizeof(buf));
        vga_puts(buf);
        if (i < 5) vga_puts(":");
    }
    vga_puts("\n");
    
    vga_puts("[E1000] Setting up PHY...\n");
    e1000_setup_phy();
    
    e1000_setup_tx();
    e1000_setup_rx();
    e1000_enable_interrupts();
    
    register_irq_handler(9, e1000_irq_handler);
    pic_enable_irq(9);
    
    vga_puts("[E1000] OK\n");
    
    return 0;
}

int e1000_send(uint8_t* data, size_t len) {
    if (e1000_io_base == NULL) return -1;
    
    if (len > E1000_BUFFER_SIZE) return -1;
    
    for (int i = 0; i < 10000; i++) {
        if (e1000_tx_descs[e1000_current_tx].status & E1000_TXD_STAT_DD) {
            break;
        }
        __asm__ volatile("nop");
    }
    
    if (!(e1000_tx_descs[e1000_current_tx].status & E1000_TXD_STAT_DD)) {
        return -1;
    }
    
    uint8_t* tx_buf = e1000_tx_buffers + e1000_current_tx * E1000_BUFFER_SIZE;
    memcpy(tx_buf, data, len);
    
    e1000_tx_descs[e1000_current_tx].len = len;
    e1000_tx_descs[e1000_current_tx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
    e1000_tx_descs[e1000_current_tx].status = 0;
    
    e1000_current_tx = (e1000_current_tx + 1) % E1000_TX_DESC_COUNT;
    
    e1000_write_reg32(0x3818, e1000_current_tx);
    
    return 0;
}

void e1000_receive() {
    while ((e1000_rx_descs[e1000_current_rx].status & E1000_RXD_STAT_DD)) {
        uint16_t len = e1000_rx_descs[e1000_current_rx].len;
        uint8_t* buf = e1000_rx_buffers + e1000_current_rx * E1000_BUFFER_SIZE;
        
        if (len > 0 && len <= E1000_BUFFER_SIZE) {
            vga_puts("[E1000] Received ");
            char buf2[8];
            itoa(len, buf2, 10, sizeof(buf2));
            vga_puts(buf2);
            vga_puts(" bytes\n");
            net_handle_packet(buf, len);
        }
        
        e1000_rx_descs[e1000_current_rx].status = 0;
        e1000_rx_descs[e1000_current_rx].addr = (uint32_t)(e1000_rx_buffers + e1000_current_rx * E1000_BUFFER_SIZE);
        
        e1000_current_rx = (e1000_current_rx + 1) % E1000_RX_DESC_COUNT;
        e1000_write_reg32(0x28B0, e1000_current_rx);
    }
}

void e1000_poll() {
    if (e1000_io_base == NULL) return;
    
    uint32_t icr = e1000_read_reg32(0x00C0);
    
    if (icr != 0) {
        e1000_receive();
        e1000_write_reg32(0x00C0, icr);
    }
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
        if (arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].valid = 1;
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    
    arp_cache[0].ip = ip;
    memcpy(arp_cache[0].mac, mac, 6);
}
