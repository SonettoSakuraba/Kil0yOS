#include "net/udp.h"
#include "net/net.h"
#include "net/dhcp.h"
#include "net/endian.h"
#include "lib/string.h"

int udp_send(uint32_t dest_ip, uint16_t dest_port, uint16_t src_port, uint8_t* data, size_t len) {
    udp_header_t udp;
    memset(&udp, 0, sizeof(udp_header_t));
    udp.src_port = htons(src_port);
    udp.dest_port = htons(dest_port);
    udp.length = htons(UDP_HEADER_LEN + len);
    udp.checksum = 0;

    size_t packet_len = sizeof(udp_header_t) + len;
    uint8_t packet[NET_BUFFER_SIZE];
    if (packet_len > NET_BUFFER_SIZE) return -1;

    memcpy(packet, &udp, sizeof(udp_header_t));
    memcpy(packet + sizeof(udp_header_t), data, len);

    return net_send_ipv4(dest_ip, IPV4_PROTO_UDP, packet, packet_len);
}

void udp_handle_packet(uint8_t* packet, size_t len) {
    if (len < sizeof(ipv4_header_t) + sizeof(udp_header_t)) return;

    udp_header_t* udp = (udp_header_t*)(packet + sizeof(ipv4_header_t));
    uint16_t dest_port = ntohs(udp->dest_port);
    size_t data_len = ntohs(udp->length) - UDP_HEADER_LEN;

    if (data_len > len - sizeof(ipv4_header_t) - sizeof(udp_header_t)) {
        data_len = len - sizeof(ipv4_header_t) - sizeof(udp_header_t);
    }

    if (dest_port == DHCP_CLIENT_PORT) {
        dhcp_handle_packet(packet + sizeof(ipv4_header_t) + sizeof(udp_header_t), data_len);
    }
}