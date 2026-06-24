#ifndef ENDIAN_H
#define ENDIAN_H

#include "lib/types.h"

static inline uint16_t htons(uint16_t host) {
    uint8_t* bytes = (uint8_t*)&host;
    return (uint16_t)((bytes[0] << 8) | bytes[1]);
}

static inline uint16_t ntohs(uint16_t net) {
    return htons(net);
}

static inline uint32_t htonl(uint32_t host) {
    uint8_t* bytes = (uint8_t*)&host;
    return (uint32_t)((bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3]);
}

static inline uint32_t ntohl(uint32_t net) {
    return htonl(net);
}

#endif