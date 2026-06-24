#ifndef E1000_H
#define E1000_H

#include "lib/types.h"

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

#define E1000_TX_DESC_COUNT 32
#define E1000_RX_DESC_COUNT 128
#define E1000_BUFFER_SIZE 2048

#define E1000_REG_CTRL     0x00000
#define E1000_REG_STATUS   0x00008
#define E1000_REG_EECD     0x00010
#define E1000_REG_EERD     0x00014
#define E1000_REG_RCTL     0x00100
#define E1000_REG_TCTL     0x00400
#define E1000_REG_RDLEN    0x00280
#define E1000_REG_RDH      0x00288
#define E1000_REG_RDT      0x00290
#define E1000_REG_TDLEN    0x00380
#define E1000_REG_TDH      0x00388
#define E1000_REG_TDT      0x00390
#define E1000_REG_IMS      0x000D0
#define E1000_REG_ICR      0x000C0
#define E1000_REG_RDTR     0x002C8
#define E1000_REG_RXDCTL   0x00320
#define E1000_REG_TXDCTL   0x00360
#define E1000_REG_PHYADDR  0x00010
#define E1000_REG_MACADDR  0x00000

#define E1000_CTRL_RST     (1 << 26)
#define E1000_CTRL_EN      (1 << 1)

#define E1000_RCTL_EN      (1 << 1)
#define E1000_RCTL_BAM     (1 << 15)
#define E1000_RCTL_SZ_2048 (0x0 << 10)
#define E1000_RCTL_SECRC   (1 << 25)

#define E1000_TCTL_EN      (1 << 1)
#define E1000_TCTL_PSP     (1 << 3)
#define E1000_TCTL_CT      (0x10 << 4)
#define E1000_TCTL_COLD    (0x40 << 12)

#define E1000_IMS_RXT0     (1 << 0)
#define E1000_IMS_TXDW     (1 << 1)

#define E1000_TXD_CMD_EOP  (1 << 0)
#define E1000_TXD_CMD_RS   (1 << 3)
#define E1000_TXD_STAT_DD  (1 << 0)

#define E1000_RXD_STAT_DD  (1 << 0)
#define E1000_RXD_STAT_EOP (1 << 1)

typedef struct e1000_tx_desc {
    uint64_t addr;
    uint16_t len;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct e1000_rx_desc {
    uint64_t addr;
    uint16_t len;
    uint16_t csum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

extern uint8_t e1000_mac[6];

int e1000_init(uint16_t io_base);
int e1000_send(uint8_t* data, size_t len);
void e1000_receive();
uint8_t* e1000_arp_cache_lookup(uint32_t ip);
void e1000_arp_cache_update(uint32_t ip, uint8_t* mac);

#endif
