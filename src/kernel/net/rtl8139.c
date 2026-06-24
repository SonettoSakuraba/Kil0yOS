#include "net/rtl8139.h"
#include "net/net.h"
#include "drivers/io.h"
#include "drivers/vga.h"
#include "core/isr.h"
#include "core/interrupts.h"
#include "mm/memory.h"
#include "lib/string.h"
#include "lib/stdlib.h"

static uint16_t rtl8139_io_base = 0;
static uint8_t* rtl8139_rx_buffer = NULL;
static uint8_t* rtl8139_tx_buffers[RTL8139_TX_BUFFER_COUNT];
static int rtl8139_current_tx_buffer = 0;
static uint16_t rtl8139_rx_ptr = 0;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

uint8_t rtl8139_mac[6];

static void rtl8139_write_reg(uint16_t offset, uint8_t value) {
    outb(rtl8139_io_base + offset, value);
}

static void rtl8139_write_reg16(uint16_t offset, uint16_t value) {
    outw(rtl8139_io_base + offset, value);
}

static void rtl8139_write_reg32(uint16_t offset, uint32_t value) {
    outd(rtl8139_io_base + offset, value);
}

static uint8_t rtl8139_read_reg(uint16_t offset) {
    return inb(rtl8139_io_base + offset);
}

static uint16_t rtl8139_read_reg16(uint16_t offset) {
    return inw(rtl8139_io_base + offset);
}

static void rtl8139_irq_handler(interrupt_frame_t* frame) {
    (void)frame;
    uint16_t isr = rtl8139_read_reg16(RTL8139_REG_ISR);

    if (isr & RTL8139_IRQ_RXE) {
        rtl8139_receive();
    }

    if (isr & RTL8139_IRQ_TXE) {
        rtl8139_write_reg16(RTL8139_REG_ISR, RTL8139_IRQ_TXE);
    }

    pic_send_eoi(11);
}

uint8_t* rtl8139_arp_cache_lookup(uint32_t ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            return arp_cache[i].mac;
        }
    }
    return NULL;
}

void rtl8139_arp_cache_update(uint32_t ip, uint8_t* mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, ETH_MAC_LEN);
            arp_cache[i].valid = 1;
            return;
        }
        if (arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, ETH_MAC_LEN);
            return;
        }
    }
    memcpy(arp_cache[0].mac, mac, ETH_MAC_LEN);
    arp_cache[0].ip = ip;
}

int rtl8139_init() {
    pci_device_t* dev = pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
    if (dev == NULL) {
        vga_puts("[RTL8139] Device not found\n");
        return -1;
    }

    rtl8139_io_base = (uint16_t)(dev->bar0 & 0xFFFF);
    vga_puts("[RTL8139] Found at IO port ");
    char buf[10];
    utoa((uint32_t)rtl8139_io_base, buf, 16, sizeof(buf));
    vga_puts(buf);
    vga_puts("\n");

    rtl8139_write_reg(RTL8139_REG_CMD, RTL8139_CMD_RESET);
    while (rtl8139_read_reg(RTL8139_REG_CMD) & RTL8139_CMD_RESET) {
        __asm__ volatile("nop");
    }

    rtl8139_mac[0] = rtl8139_read_reg(RTL8139_REG_IDR0);
    rtl8139_mac[1] = rtl8139_read_reg(RTL8139_REG_IDR1);
    rtl8139_mac[2] = rtl8139_read_reg(RTL8139_REG_IDR2);
    rtl8139_mac[3] = rtl8139_read_reg(RTL8139_REG_IDR3);
    rtl8139_mac[4] = rtl8139_read_reg(RTL8139_REG_IDR4);
    rtl8139_mac[5] = rtl8139_read_reg(RTL8139_REG_IDR5);

    memcpy(net_interface.mac, rtl8139_mac, ETH_MAC_LEN);

    rtl8139_rx_buffer = (uint8_t*)kmalloc(RTL8139_RX_BUFFER_SIZE);
    if (rtl8139_rx_buffer == NULL) {
        vga_puts("[RTL8139] Failed to allocate RX buffer\n");
        return -1;
    }
    memset(rtl8139_rx_buffer, 0, RTL8139_RX_BUFFER_SIZE);
    rtl8139_write_reg32(RTL8139_REG_RBSTART, (uint32_t)rtl8139_rx_buffer);

    for (int i = 0; i < RTL8139_TX_BUFFER_COUNT; i++) {
        rtl8139_tx_buffers[i] = (uint8_t*)kmalloc(RTL8139_TX_BUFFER_SIZE);
        if (rtl8139_tx_buffers[i] == NULL) {
            vga_puts("[RTL8139] Failed to allocate TX buffer\n");
            return -1;
        }
        rtl8139_write_reg32(RTL8139_REG_TB0START + i * 4, (uint32_t)rtl8139_tx_buffers[i]);
    }

    rtl8139_write_reg(RTL8139_REG_RCR, 0x0C);
    rtl8139_write_reg16(RTL8139_REG_IMR, RTL8139_IRQ_RXE | RTL8139_IRQ_TXE);

    register_irq_handler(11, rtl8139_irq_handler);
    pic_enable_irq(11);

    rtl8139_write_reg(RTL8139_REG_CMD, RTL8139_CMD_RX_EN | RTL8139_CMD_TX_EN);

    net_interface.initialized = 1;
    net_interface.link_up = 1;

    vga_puts("[RTL8139] Initialized successfully\n");
    return 0;
}

int rtl8139_send(uint8_t* data, size_t len) {
    if (rtl8139_io_base == 0) return -1;

    if (len > RTL8139_TX_BUFFER_SIZE) return -1;

    uint8_t* tx_buf = rtl8139_tx_buffers[rtl8139_current_tx_buffer];
    memcpy(tx_buf, data, len);

    uint16_t status = rtl8139_read_reg(RTL8139_REG_TSR0 + rtl8139_current_tx_buffer * 2);
    if (!(status & 0x80)) {
        return -1;
    }

    rtl8139_write_reg32(RTL8139_REG_TB0START + rtl8139_current_tx_buffer * 4, (uint32_t)tx_buf);

    uint32_t send_cmd = ((len & 0x1FFF) << 16) | RTL8139_TX_CMD_START;
    rtl8139_write_reg32(RTL8139_REG_TB0START + rtl8139_current_tx_buffer * 4, send_cmd);

    rtl8139_current_tx_buffer = (rtl8139_current_tx_buffer + 1) % RTL8139_TX_BUFFER_COUNT;

    return 0;
}

void rtl8139_receive() {
    if (rtl8139_io_base == 0) return;

    while (rtl8139_read_reg(RTL8139_REG_RSR) & 0x01) {
        uint16_t len = *(uint16_t*)(rtl8139_rx_buffer + rtl8139_rx_ptr + 2);

        if (len > 0 && len < RTL8139_RX_BUFFER_SIZE) {
            uint8_t packet[NET_BUFFER_SIZE];
            if (len > NET_BUFFER_SIZE) len = NET_BUFFER_SIZE;
            memcpy(packet, rtl8139_rx_buffer + rtl8139_rx_ptr + 4, len);
            net_handle_packet(packet, len);
        }

        rtl8139_rx_ptr = (rtl8139_rx_ptr + len + 4 + 3) & ~3;
        if (rtl8139_rx_ptr >= RTL8139_RX_BUFFER_SIZE) {
            rtl8139_rx_ptr = 0;
        }

        rtl8139_write_reg16(RTL8139_REG_RX_CNT, rtl8139_rx_ptr);
    }
}