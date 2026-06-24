#ifndef UDP_H
#define UDP_H

#include "lib/types.h"

#define UDP_HEADER_LEN 8

typedef struct udp_header {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

int udp_send(uint32_t dest_ip, uint16_t dest_port, uint16_t src_port, uint8_t* data, size_t len);
void udp_handle_packet(uint8_t* packet, size_t len);

#endif