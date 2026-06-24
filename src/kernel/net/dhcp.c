#include "net/dhcp.h"
#include "net/net.h"
#include "net/udp.h"
#include "net/endian.h"
#include "drivers/vga.h"
#include "lib/string.h"
#include "lib/stdlib.h"

static uint32_t dhcp_xid = 0x12345678;

static void dhcp_add_option(uint8_t* options, int* offset, uint8_t code, uint8_t len, void* data) {
    options[(*offset)++] = code;
    if (len > 0) {
        options[(*offset)++] = len;
        memcpy(options + *offset, data, len);
        *offset += len;
    }
}

static uint32_t dhcp_parse_option(uint8_t* options, uint8_t code) {
    int i = 0;
    while (i < 312) {
        if (options[i] == DHCP_OPT_END) break;
        if (options[i] == code) {
            if (options[i + 1] == 4) {
                return *(uint32_t*)(options + i + 2);
            }
            return 0;
        }
        i += options[i + 1] + 2;
    }
    return 0;
}

static int dhcp_send_message(uint8_t msg_type, uint32_t requested_ip, uint32_t server_ip) {
    dhcp_header_t dhcp;
    memset(&dhcp, 0, sizeof(dhcp_header_t));

    dhcp.op = DHCP_OP_BOOTREQUEST;
    dhcp.htype = 1;
    dhcp.hlen = ETH_MAC_LEN;
    dhcp.hops = 0;
    dhcp.xid = dhcp_xid;
    dhcp.secs = 0;
    dhcp.flags = htons(0x8000);
    dhcp.ciaddr = 0;
    dhcp.yiaddr = 0;
    dhcp.siaddr = 0;
    dhcp.giaddr = 0;
    memcpy(dhcp.chaddr, net_interface.mac, ETH_MAC_LEN);
    dhcp.magic_cookie = DHCP_OPT_MAGIC_COOKIE;

    int opt_offset = 0;
    dhcp_add_option(dhcp.options, &opt_offset, DHCP_OPT_MESSAGE_TYPE, 1, &msg_type);

    if (msg_type == DHCP_MSG_REQUEST) {
        if (requested_ip != 0) {
            dhcp_add_option(dhcp.options, &opt_offset, DHCP_OPT_REQUESTED_IP, 4, &requested_ip);
        }
        if (server_ip != 0) {
            dhcp_add_option(dhcp.options, &opt_offset, DHCP_OPT_SERVER_ID, 4, &server_ip);
        }
    }

    uint8_t req_list[] = {DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_DNS, 0};
    dhcp_add_option(dhcp.options, &opt_offset, 55, sizeof(req_list) - 1, req_list);
    dhcp_add_option(dhcp.options, &opt_offset, DHCP_OPT_END, 0, NULL);

    return udp_send(0xFFFFFFFF, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, (uint8_t*)&dhcp, sizeof(dhcp_header_t));
}

void dhcp_handle_packet(uint8_t* packet, size_t len) {
    if (len < sizeof(dhcp_header_t)) return;

    dhcp_header_t* dhcp = (dhcp_header_t*)packet;
    if (dhcp->magic_cookie != DHCP_OPT_MAGIC_COOKIE) return;
    if (dhcp->op != DHCP_OP_BOOTREPLY) return;
    if (dhcp->xid != dhcp_xid) return;

    uint8_t msg_type = dhcp_parse_option(dhcp->options, DHCP_OPT_MESSAGE_TYPE);

    if (msg_type == DHCP_MSG_OFFER) {
        vga_puts("[DHCP] Received offer: ");
        char ip_buf[16];
        net_ip_to_str(dhcp->yiaddr, ip_buf);
        vga_puts(ip_buf);
        vga_puts("\n");

        uint32_t server_ip = dhcp_parse_option(dhcp->options, DHCP_OPT_SERVER_ID);
        dhcp_request(dhcp->yiaddr, server_ip);
    } else if (msg_type == DHCP_MSG_ACK) {
        vga_puts("[DHCP] ACK received\n");

        net_interface.ip = dhcp->yiaddr;
        net_interface.subnet = dhcp_parse_option(dhcp->options, DHCP_OPT_SUBNET_MASK);
        net_interface.gateway = dhcp_parse_option(dhcp->options, DHCP_OPT_ROUTER);
        net_interface.dns = dhcp_parse_option(dhcp->options, DHCP_OPT_DNS);

        vga_puts("[DHCP] IP: ");
        char ip_buf[16];
        net_ip_to_str(net_interface.ip, ip_buf);
        vga_puts(ip_buf);
        vga_puts("\n");

        vga_puts("[DHCP] Subnet: ");
        net_ip_to_str(net_interface.subnet, ip_buf);
        vga_puts(ip_buf);
        vga_puts("\n");

        vga_puts("[DHCP] Gateway: ");
        net_ip_to_str(net_interface.gateway, ip_buf);
        vga_puts(ip_buf);
        vga_puts("\n");

        vga_puts("[DHCP] DNS: ");
        net_ip_to_str(net_interface.dns, ip_buf);
        vga_puts(ip_buf);
        vga_puts("\n");
    }
}

int dhcp_discover() {
    vga_puts("[DHCP] Sending discover...\n");
    dhcp_xid = rand();
    return dhcp_send_message(DHCP_MSG_DISCOVER, 0, 0);
}

int dhcp_request(uint32_t requested_ip, uint32_t server_ip) {
    vga_puts("[DHCP] Sending request...\n");
    return dhcp_send_message(DHCP_MSG_REQUEST, requested_ip, server_ip);
}