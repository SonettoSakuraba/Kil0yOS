#ifndef RTL8139_H
#define RTL8139_H

#include "lib/types.h"
#include "drivers/pci.h"

#define RTL8139_RX_BUFFER_SIZE 8192
#define RTL8139_TX_BUFFER_COUNT 4
#define RTL8139_TX_BUFFER_SIZE 2048

#define RTL8139_REG_IDR0       0x00
#define RTL8139_REG_IDR1       0x01
#define RTL8139_REG_IDR2       0x02
#define RTL8139_REG_IDR3       0x03
#define RTL8139_REG_IDR4       0x04
#define RTL8139_REG_IDR5       0x05

#define RTL8139_REG_RBSTART    0x30
#define RTL8139_REG_CMD        0x37

#define RTL8139_REG_TCR        0x40
#define RTL8139_REG_TSR0       0x41
#define RTL8139_REG_TSR1       0x42
#define RTL8139_REG_TSR2       0x43
#define RTL8139_REG_TSR3       0x44

#define RTL8139_REG_TB0START   0x20
#define RTL8139_REG_TB1START   0x24
#define RTL8139_REG_TB2START   0x28
#define RTL8139_REG_TB3START   0x2C

#define RTL8139_REG_RCR        0x4C
#define RTL8139_REG_RSR        0x50
#define RTL8139_REG_RX_CNT     0x58

#define RTL8139_REG_IMR        0x3C
#define RTL8139_REG_ISR        0x3E

#define RTL8139_CMD_RESET      0x10
#define RTL8139_CMD_RX_EN      0x08
#define RTL8139_CMD_TX_EN      0x04

#define RTL8139_TX_CMD_START   0x00000001

#define RTL8139_IRQ_RXE        0x01
#define RTL8139_IRQ_TXE        0x02

#define ARP_CACHE_SIZE 16

typedef struct arp_entry {
    uint32_t ip;
    uint8_t mac[6];
    int valid;
} arp_entry_t;

extern uint8_t rtl8139_mac[6];

int rtl8139_init();
int rtl8139_send(uint8_t* data, size_t len);
void rtl8139_receive();
uint8_t* rtl8139_arp_cache_lookup(uint32_t ip);
void rtl8139_arp_cache_update(uint32_t ip, uint8_t* mac);

#endif