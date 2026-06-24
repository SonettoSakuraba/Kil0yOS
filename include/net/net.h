#ifndef NET_H
#define NET_H

#include "lib/types.h"

#define NET_BUFFER_SIZE 1536
#define NET_MAX_PACKETS 64

#define ETH_MAC_LEN 6
#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP 0x0806

#define IPV4_HEADER_LEN 20
#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_TCP 6
#define IPV4_PROTO_UDP 17

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY 0

#define ARP_REQUEST 1
#define ARP_REPLY 2

#define NET_IFACE_NAME "eth0"

typedef struct eth_header {
    uint8_t dest_mac[ETH_MAC_LEN];
    uint8_t src_mac[ETH_MAC_LEN];
    uint16_t eth_type;
} __attribute__((packed)) eth_header_t;

typedef struct arp_header {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t hw_len;
    uint8_t proto_len;
    uint16_t op_code;
    uint8_t sender_mac[ETH_MAC_LEN];
    uint32_t sender_ip;
    uint8_t target_mac[ETH_MAC_LEN];
    uint32_t target_ip;
} __attribute__((packed)) arp_header_t;

typedef struct ipv4_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed)) ipv4_header_t;

typedef struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_header_t;

typedef struct net_iface {
    uint8_t mac[ETH_MAC_LEN];
    uint32_t ip;
    uint32_t subnet;
    uint32_t gateway;
    uint32_t dns;
    int initialized;
    int link_up;
    
    int (*send_packet)(uint8_t* data, size_t len);
    uint8_t* (*resolve_mac)(uint32_t ip);
    void (*update_arp_cache)(uint32_t ip, uint8_t* mac);
} net_iface_t;

extern net_iface_t net_interface;

void net_init();
int net_send_packet(uint8_t* data, size_t len);
int net_send_arp_request(uint32_t target_ip);
int net_send_icmp_echo(uint32_t dest_ip, uint16_t id, uint16_t seq);
uint8_t* net_resolve_mac(uint32_t ip);
uint32_t net_str_to_ip(const char* str);
void net_ip_to_str(uint32_t ip, char* buf);
int net_send_ipv4(uint32_t dest_ip, uint8_t protocol, uint8_t* data, size_t data_len);
void net_handle_packet(uint8_t* packet, size_t len);

#endif