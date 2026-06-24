#include "net/net.h"
#include "net/rtl8139.h"
#include "net/e1000.h"
#include "net/udp.h"
#include "net/endian.h"
#include "drivers/vga.h"
#include "drivers/pci.h"
#include "lib/string.h"
#include "lib/stdlib.h"

net_iface_t net_interface;

static uint8_t net_packet_buffer[NET_BUFFER_SIZE];
static uint16_t ip_id_counter = 1;

uint16_t net_checksum(uint16_t* data, size_t len) {
    uint32_t sum = 0;
    while (len > 1) {
        sum += *data++;
        len -= 2;
    }
    if (len > 0) {
        sum += *(uint8_t*)data;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum;
}

uint32_t net_str_to_ip(const char* str) {
    uint32_t ip = 0;
    uint32_t part = 0;
    int shift = 24;

    while (*str) {
        if (*str == '.') {
            ip |= part << shift;
            part = 0;
            shift -= 8;
        } else if (*str >= '0' && *str <= '9') {
            part = part * 10 + (*str - '0');
        }
        str++;
    }
    ip |= part;
    return ip;
}

void net_ip_to_str(uint32_t ip, char* buf) {
    uint8_t* bytes = (uint8_t*)&ip;
    itoa(bytes[3], buf, 10, 4);
    strcat(buf, ".");
    itoa(bytes[2], buf + strlen(buf), 10, 4);
    strcat(buf, ".");
    itoa(bytes[1], buf + strlen(buf), 10, 4);
    strcat(buf, ".");
    itoa(bytes[0], buf + strlen(buf), 10, 4);
}

int net_send_packet(uint8_t* data, size_t len) {
    if (!net_interface.initialized || !net_interface.link_up) {
        return -1;
    }
    if (net_interface.send_packet != NULL) {
        return net_interface.send_packet(data, len);
    }
    return -1;
}

int net_send_arp_request(uint32_t target_ip) {
    if (!net_interface.initialized) return -1;

    arp_header_t arp;
    memset(&arp, 0, sizeof(arp_header_t));
    arp.hw_type = htons(1);
    arp.proto_type = htons(ETH_TYPE_IPV4);
    arp.hw_len = ETH_MAC_LEN;
    arp.proto_len = 4;
    arp.op_code = htons(ARP_REQUEST);
    memcpy(arp.sender_mac, net_interface.mac, ETH_MAC_LEN);
    arp.sender_ip = net_interface.ip;
    memset(arp.target_mac, 0, ETH_MAC_LEN);
    arp.target_ip = target_ip;

    eth_header_t eth;
    memset(eth.dest_mac, 0xFF, ETH_MAC_LEN);
    memcpy(eth.src_mac, net_interface.mac, ETH_MAC_LEN);
    eth.eth_type = htons(ETH_TYPE_ARP);

    uint8_t packet[sizeof(eth_header_t) + sizeof(arp_header_t)];
    memcpy(packet, &eth, sizeof(eth_header_t));
    memcpy(packet + sizeof(eth_header_t), &arp, sizeof(arp_header_t));

    return net_send_packet(packet, sizeof(packet));
}

int net_send_ipv4(uint32_t dest_ip, uint8_t protocol, uint8_t* data, size_t data_len) {
    if (!net_interface.initialized) return -1;

    uint8_t dest_mac[ETH_MAC_LEN];
    
    if (dest_ip == 0xFFFFFFFF) {
        memset(dest_mac, 0xFF, ETH_MAC_LEN);
    } else {
        uint8_t* resolved_mac = net_resolve_mac(dest_ip);
        if (resolved_mac == NULL) {
            net_send_arp_request(dest_ip);
            for (int i = 0; i < 1000000; i++) {
                __asm__ volatile("nop");
            }
            resolved_mac = net_resolve_mac(dest_ip);
            if (resolved_mac == NULL) return -1;
        }
        memcpy(dest_mac, resolved_mac, ETH_MAC_LEN);
    }

    ipv4_header_t ip;
    memset(&ip, 0, sizeof(ipv4_header_t));
    ip.version_ihl = 0x45;
    ip.tos = 0;
    ip.total_len = htons(IPV4_HEADER_LEN + data_len);
    ip.id = htons(ip_id_counter++);
    ip.flags_frag = 0;
    ip.ttl = 64;
    ip.protocol = protocol;
    ip.src_ip = net_interface.ip;
    ip.dest_ip = dest_ip;
    ip.checksum = net_checksum((uint16_t*)&ip, sizeof(ipv4_header_t));

    eth_header_t eth;
    memcpy(eth.dest_mac, dest_mac, ETH_MAC_LEN);
    memcpy(eth.src_mac, net_interface.mac, ETH_MAC_LEN);
    eth.eth_type = htons(ETH_TYPE_IPV4);

    size_t packet_len = sizeof(eth_header_t) + sizeof(ipv4_header_t) + data_len;
    uint8_t packet[NET_BUFFER_SIZE];
    if (packet_len > NET_BUFFER_SIZE) return -1;

    memcpy(packet, &eth, sizeof(eth_header_t));
    memcpy(packet + sizeof(eth_header_t), &ip, sizeof(ipv4_header_t));
    memcpy(packet + sizeof(eth_header_t) + sizeof(ipv4_header_t), data, data_len);

    return net_send_packet(packet, packet_len);
}

int net_send_icmp_echo(uint32_t dest_ip, uint16_t id, uint16_t seq) {
    icmp_header_t icmp;
    memset(&icmp, 0, sizeof(icmp_header_t));
    icmp.type = ICMP_ECHO_REQUEST;
    icmp.code = 0;
    icmp.id = htons(id);
    icmp.seq = htons(seq);
    icmp.checksum = net_checksum((uint16_t*)&icmp, sizeof(icmp_header_t));

    return net_send_ipv4(dest_ip, IPV4_PROTO_ICMP, (uint8_t*)&icmp, sizeof(icmp_header_t));
}

static void net_handle_arp(uint8_t* packet, size_t len) {
    if (len < sizeof(eth_header_t) + sizeof(arp_header_t)) return;

    arp_header_t* arp = (arp_header_t*)(packet + sizeof(eth_header_t));

    if (ntohs(arp->op_code) == ARP_REPLY) {
        if (net_interface.update_arp_cache != NULL) {
            net_interface.update_arp_cache(arp->sender_ip, arp->sender_mac);
        }
    } else if (ntohs(arp->op_code) == ARP_REQUEST) {
        if (arp->target_ip == net_interface.ip) {
            arp_header_t reply;
            memcpy(&reply, arp, sizeof(arp_header_t));
            reply.op_code = htons(ARP_REPLY);
            memcpy(reply.sender_mac, net_interface.mac, ETH_MAC_LEN);
            reply.sender_ip = net_interface.ip;
            memcpy(reply.target_mac, arp->sender_mac, ETH_MAC_LEN);
            reply.target_ip = arp->sender_ip;

            eth_header_t eth;
            memcpy(eth.dest_mac, arp->sender_mac, ETH_MAC_LEN);
            memcpy(eth.src_mac, net_interface.mac, ETH_MAC_LEN);
            eth.eth_type = htons(ETH_TYPE_ARP);

            uint8_t reply_packet[sizeof(eth_header_t) + sizeof(arp_header_t)];
            memcpy(reply_packet, &eth, sizeof(eth_header_t));
            memcpy(reply_packet + sizeof(eth_header_t), &reply, sizeof(arp_header_t));

            net_send_packet(reply_packet, sizeof(reply_packet));
        }
    }
}

static void net_handle_icmp(uint8_t* packet, size_t len) {
    if (len < sizeof(eth_header_t) + sizeof(ipv4_header_t) + sizeof(icmp_header_t)) return;

    ipv4_header_t* ip = (ipv4_header_t*)(packet + sizeof(eth_header_t));
    icmp_header_t* icmp = (icmp_header_t*)(packet + sizeof(eth_header_t) + sizeof(ipv4_header_t));

    if (icmp->type == ICMP_ECHO_REQUEST) {
        icmp->type = ICMP_ECHO_REPLY;
        icmp->checksum = 0;
        icmp->checksum = net_checksum((uint16_t*)icmp, len - sizeof(eth_header_t) - sizeof(ipv4_header_t));

        uint32_t temp_ip = ip->src_ip;
        ip->src_ip = ip->dest_ip;
        ip->dest_ip = temp_ip;
        ip->checksum = 0;
        ip->checksum = net_checksum((uint16_t*)ip, sizeof(ipv4_header_t));

        uint8_t temp_mac[ETH_MAC_LEN];
        eth_header_t* eth = (eth_header_t*)packet;
        memcpy(temp_mac, eth->dest_mac, ETH_MAC_LEN);
        memcpy(eth->dest_mac, eth->src_mac, ETH_MAC_LEN);
        memcpy(eth->src_mac, temp_mac, ETH_MAC_LEN);

        net_send_packet(packet, len);
    }
}

static void net_handle_ipv4(uint8_t* packet, size_t len) {
    if (len < sizeof(eth_header_t) + sizeof(ipv4_header_t)) return;
    
    ipv4_header_t* ip = (ipv4_header_t*)(packet + sizeof(eth_header_t));
    
    if (ip->version_ihl != 0x45) return;
    
    switch (ip->protocol) {
        case IPV4_PROTO_ICMP:
            net_handle_icmp(packet, len);
            break;
        case IPV4_PROTO_UDP:
            udp_handle_packet(packet, len);
            break;
    }
}

void net_handle_packet(uint8_t* packet, size_t len) {
    if (len < sizeof(eth_header_t)) return;

    eth_header_t* eth = (eth_header_t*)packet;

    switch (ntohs(eth->eth_type)) {
        case ETH_TYPE_ARP:
            net_handle_arp(packet, len);
            break;
        case ETH_TYPE_IPV4:
            net_handle_ipv4(packet, len);
            break;
    }
}

uint8_t* net_resolve_mac(uint32_t ip) {
    if (net_interface.resolve_mac != NULL) {
        return net_interface.resolve_mac(ip);
    }
    return NULL;
}

void net_init() {
    memset(&net_interface, 0, sizeof(net_iface_t));
    
    pci_device_t* dev = pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (dev != NULL) {
        if (e1000_init(dev->bar0) == 0) {
            memcpy(net_interface.mac, e1000_mac, ETH_MAC_LEN);
            net_interface.initialized = 1;
            net_interface.link_up = 1;
            net_interface.send_packet = e1000_send;
            net_interface.resolve_mac = e1000_arp_cache_lookup;
            net_interface.update_arp_cache = e1000_arp_cache_update;
            
            vga_puts("[NET] Network interface initialized\n");
            vga_puts("[NET] MAC: ");
            for (int i = 0; i < ETH_MAC_LEN; i++) {
                char buf[3];
                utoa(net_interface.mac[i], buf, 16, sizeof(buf));
                vga_puts(buf);
                if (i < ETH_MAC_LEN - 1) vga_puts(":");
            }
            vga_puts("\n");
            return;
        }
    }
    
    if (rtl8139_init() == 0) {
        memcpy(net_interface.mac, rtl8139_mac, ETH_MAC_LEN);
        net_interface.initialized = 1;
        net_interface.link_up = 1;
        net_interface.send_packet = rtl8139_send;
        net_interface.resolve_mac = rtl8139_arp_cache_lookup;
        net_interface.update_arp_cache = rtl8139_arp_cache_update;
        
        vga_puts("[NET] Network interface initialized\n");
        vga_puts("[NET] MAC: ");
        for (int i = 0; i < ETH_MAC_LEN; i++) {
            char buf[3];
            utoa(net_interface.mac[i], buf, 16, sizeof(buf));
            vga_puts(buf);
            if (i < ETH_MAC_LEN - 1) vga_puts(":");
        }
        vga_puts("\n");
    } else {
        vga_puts("[NET] Failed to initialize network interface\n");
    }
}