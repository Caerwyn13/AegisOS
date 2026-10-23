#ifndef NET_H
#define NET_H

#include "types.h"

#define NET_PING_COUNT 4

typedef struct {
    uint32_t ip_address;
    uint32_t gateway;
    uint32_t netmask;
    uint8_t mac[6];
    uint8_t available;
} net_interface_info_t;

typedef struct {
    uint8_t received;
    uint8_t reply_mask;
    uint32_t rtt_ms[NET_PING_COUNT];
} net_ping_result_t;

void net_init(void);
void net_get_interface(net_interface_info_t *info);
int net_parse_ipv4(const char *text, uint32_t *address);
int net_ping_ipv4(uint32_t destination, net_ping_result_t *result);

#endif
