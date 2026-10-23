#include "net.h"
#include "e1000.h"
#include "pit.h"
#include "string.h"

#define ETHERNET_HEADER_SIZE 14
#define IPV4_HEADER_SIZE 20
#define ICMP_HEADER_SIZE 8
#define ICMP_PAYLOAD_SIZE 56
#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_ARP 0x0806
#define IPV4_PROTOCOL_ICMP 1
#define ARP_PACKET_SIZE 28
#define PACKET_BUFFER_SIZE 1518
#define ARP_TIMEOUT_MS 2000
#define PING_TIMEOUT_MS 1000
#define PING_INTERVAL_MS 1000

typedef struct __attribute__((packed)) {
    uint8_t destination[6];
    uint8_t source[6];
    uint16_t type;
} ethernet_header_t;

static net_interface_info_t interface_info;
static uint8_t packet_buffer[PACKET_BUFFER_SIZE];
static uint8_t arp_mac[6];
static uint32_t arp_ip;
static uint16_t next_identifier = 1;

static uint16_t read_be16(const uint8_t *data) {
    return ((uint16_t)data[0] << 8) | data[1];
}

static void write_be16(uint8_t *data, uint16_t value) {
    data[0] = value >> 8;
    data[1] = value & 0xFF;
}

static uint32_t read_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void write_be32(uint8_t *data, uint32_t value) {
    data[0] = value >> 24;
    data[1] = value >> 16;
    data[2] = value >> 8;
    data[3] = value;
}

static uint16_t checksum(const uint8_t *data, uint32_t length) {
    uint32_t sum = 0;
    while (length > 1) {
        sum += ((uint16_t)data[0] << 8) | data[1];
        data += 2;
        length -= 2;
    }
    if (length)
        sum += (uint16_t)data[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void copy_mac(uint8_t *destination, const uint8_t *source) {
    for (uint32_t i = 0; i < 6; i++)
        destination[i] = source[i];
}

void net_init(void) {
    memset(&interface_info, 0, sizeof(interface_info));

    if (e1000_init(interface_info.mac) != 0)
        return;

    interface_info.ip_address = 0x0A00020F;
    interface_info.gateway = 0x0A000202;
    interface_info.netmask = 0xFFFFFF00;
    interface_info.available = 1;
}

void net_get_interface(net_interface_info_t *info) {
    if (info)
        *info = interface_info;
}

int net_parse_ipv4(const char *text, uint32_t *address) {
    if (!text || !address)
        return -1;

    uint32_t octets[4] = {0, 0, 0, 0};
    uint32_t part = 0;
    uint32_t digits = 0;
    uint32_t index = 0;

    while (*text) {
        if (*text >= '0' && *text <= '9') {
            part = part * 10 + (uint32_t)(*text - '0');
            if (part > 255 || ++digits > 3)
                return -1;
        } else if (*text == '.' && index < 3 && digits > 0) {
            octets[index++] = part;
            part = 0;
            digits = 0;
        } else {
            return -1;
        }
        text++;
    }

    if (index != 3 || digits == 0)
        return -1;
    octets[index] = part;
    *address = (octets[0] << 24) | (octets[1] << 16) |
               (octets[2] << 8) | octets[3];
    return 0;
}

static uint32_t route_ip(uint32_t destination) {
    if ((destination & interface_info.netmask) ==
        (interface_info.ip_address & interface_info.netmask))
        return destination;
    return interface_info.gateway;
}

static int send_arp_request(uint32_t target_ip) {
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (uint32_t i = 0; i < sizeof(ethernet_header_t) + ARP_PACKET_SIZE; i++)
        packet_buffer[i] = 0;

    ethernet_header_t *ethernet = (ethernet_header_t *)packet_buffer;
    copy_mac(ethernet->destination, broadcast);
    copy_mac(ethernet->source, interface_info.mac);
    ethernet->type = __builtin_bswap16(ETHERNET_TYPE_ARP);

    uint8_t *arp = packet_buffer + ETHERNET_HEADER_SIZE;
    write_be16(arp, 1);
    write_be16(arp + 2, ETHERNET_TYPE_IPV4);
    arp[4] = 6;
    arp[5] = 4;
    write_be16(arp + 6, 1);
    copy_mac(arp + 8, interface_info.mac);
    write_be32(arp + 14, interface_info.ip_address);
    write_be32(arp + 24, target_ip);
    return e1000_send(packet_buffer, ETHERNET_HEADER_SIZE + ARP_PACKET_SIZE);
}

static void process_arp(const uint8_t *frame, uint16_t length) {
    if (length < ETHERNET_HEADER_SIZE + ARP_PACKET_SIZE)
        return;

    const uint8_t *arp = frame + ETHERNET_HEADER_SIZE;
    if (read_be16(arp) != 1 ||
        read_be16(arp + 2) != ETHERNET_TYPE_IPV4 ||
        arp[4] != 6 || arp[5] != 4)
        return;

    uint16_t operation = read_be16(arp + 6);
    uint32_t sender_ip = read_be32(arp + 14);
    if (operation == 2 && sender_ip == arp_ip) {
        copy_mac(arp_mac, arp + 8);
        return;
    }

    if (operation != 1 || read_be32(arp + 24) != interface_info.ip_address)
        return;

    for (uint32_t i = 0; i < ETHERNET_HEADER_SIZE + ARP_PACKET_SIZE; i++)
        packet_buffer[i] = 0;
    ethernet_header_t *ethernet = (ethernet_header_t *)packet_buffer;
    copy_mac(ethernet->destination, arp + 8);
    copy_mac(ethernet->source, interface_info.mac);
    ethernet->type = __builtin_bswap16(ETHERNET_TYPE_ARP);

    uint8_t *reply = packet_buffer + ETHERNET_HEADER_SIZE;
    write_be16(reply, 1);
    write_be16(reply + 2, ETHERNET_TYPE_IPV4);
    reply[4] = 6;
    reply[5] = 4;
    write_be16(reply + 6, 2);
    copy_mac(reply + 8, interface_info.mac);
    write_be32(reply + 14, interface_info.ip_address);
    copy_mac(reply + 18, arp + 8);
    write_be32(reply + 24, sender_ip);
    (void)e1000_send(packet_buffer, ETHERNET_HEADER_SIZE + ARP_PACKET_SIZE);
}

static int resolve_mac(uint32_t target_ip, uint8_t *mac_out) {
    if (arp_ip == target_ip) {
        copy_mac(mac_out, arp_mac);
        return 0;
    }

    arp_ip = target_ip;
    memset(arp_mac, 0, sizeof(arp_mac));
    if (send_arp_request(target_ip) != 0)
        return -1;

    uint32_t start = pit_ticks();
    while ((uint32_t)(pit_ticks() - start) < ARP_TIMEOUT_MS) {
        int length = e1000_receive(packet_buffer, sizeof(packet_buffer));
        if (length > 0) {
            if (read_be16(packet_buffer + 12) == ETHERNET_TYPE_ARP)
                process_arp(packet_buffer, length);
            if (arp_mac[0] || arp_mac[1] || arp_mac[2] ||
                arp_mac[3] || arp_mac[4] || arp_mac[5]) {
                copy_mac(mac_out, arp_mac);
                return 0;
            }
        }
        pit_sleep(1);
    }
    arp_ip = 0;
    return -1;
}

static int send_echo(uint32_t destination, const uint8_t *destination_mac,
                     uint16_t identifier, uint16_t sequence) {
    uint32_t frame_length = ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE +
                            ICMP_HEADER_SIZE + ICMP_PAYLOAD_SIZE;
    memset(packet_buffer, 0, frame_length);

    ethernet_header_t *ethernet = (ethernet_header_t *)packet_buffer;
    copy_mac(ethernet->destination, destination_mac);
    copy_mac(ethernet->source, interface_info.mac);
    ethernet->type = __builtin_bswap16(ETHERNET_TYPE_IPV4);

    uint8_t *ip = packet_buffer + ETHERNET_HEADER_SIZE;
    ip[0] = 0x45;
    write_be16(ip + 2, IPV4_HEADER_SIZE + ICMP_HEADER_SIZE + ICMP_PAYLOAD_SIZE);
    write_be16(ip + 4, sequence);
    write_be16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = IPV4_PROTOCOL_ICMP;
    write_be32(ip + 12, interface_info.ip_address);
    write_be32(ip + 16, destination);
    write_be16(ip + 10, checksum(ip, IPV4_HEADER_SIZE));

    uint8_t *icmp = ip + IPV4_HEADER_SIZE;
    icmp[0] = 8;
    write_be16(icmp + 4, identifier);
    write_be16(icmp + 6, sequence);
    for (uint32_t i = 0; i < ICMP_PAYLOAD_SIZE; i++)
        icmp[ICMP_HEADER_SIZE + i] = (uint8_t)(i + sequence);
    write_be16(icmp + 2, checksum(icmp, ICMP_HEADER_SIZE + ICMP_PAYLOAD_SIZE));

    return e1000_send(packet_buffer, frame_length);
}

static int is_echo_reply(const uint8_t *frame, uint16_t length,
                         uint32_t destination, uint16_t identifier,
                         uint16_t sequence) {
    if (length < ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE + ICMP_HEADER_SIZE ||
        read_be16(frame + 12) != ETHERNET_TYPE_IPV4)
        return 0;

    const uint8_t *ip = frame + ETHERNET_HEADER_SIZE;
    uint32_t ip_header_size = (ip[0] & 0x0F) * 4;
    if ((ip[0] >> 4) != 4 || ip_header_size < IPV4_HEADER_SIZE ||
        length < ETHERNET_HEADER_SIZE + ip_header_size + ICMP_HEADER_SIZE ||
        ip[9] != IPV4_PROTOCOL_ICMP ||
        read_be32(ip + 16) != interface_info.ip_address ||
        (read_be16(ip + 6) & 0x3FFF) != 0)
        return 0;

    uint16_t ip_length = read_be16(ip + 2);
    if (ip_length < ip_header_size + ICMP_HEADER_SIZE ||
        ip_length > length - ETHERNET_HEADER_SIZE ||
        checksum(ip, ip_header_size) != 0)
        return 0;

    const uint8_t *icmp = ip + ip_header_size;
    uint16_t icmp_length = ip_length - ip_header_size;
    if (icmp[0] != 0 || icmp[1] != 0 ||
        read_be32(ip + 12) != destination ||
        read_be16(icmp + 4) != identifier ||
        read_be16(icmp + 6) != sequence ||
        checksum(icmp, icmp_length) != 0)
        return 0;
    return 1;
}

int net_ping_ipv4(uint32_t destination, net_ping_result_t *result) {
    if (!result || !interface_info.available)
        return -1;

    memset(result, 0, sizeof(*result));
    uint32_t next_hop = route_ip(destination);
    uint8_t destination_mac[6];
    if (resolve_mac(next_hop, destination_mac) != 0)
        return -2;

    uint16_t identifier = next_identifier++;
    for (uint16_t sequence = 0; sequence < NET_PING_COUNT; sequence++) {
        uint32_t start = pit_ticks();
        if (send_echo(destination, destination_mac, identifier, sequence) != 0)
            return -3;

        uint32_t deadline = start + PING_TIMEOUT_MS;
        while ((int32_t)(pit_ticks() - deadline) < 0) {
            int length = e1000_receive(packet_buffer, sizeof(packet_buffer));
            if (length > 0 &&
                is_echo_reply(packet_buffer, length, destination,
                              identifier, sequence)) {
                result->rtt_ms[sequence] = pit_ticks() - start;
                result->received++;
                result->reply_mask |= 1u << sequence;
                break;
            }
            pit_sleep(1);
        }

        if (sequence + 1 < NET_PING_COUNT)
            pit_sleep(PING_INTERVAL_MS);
    }

    return 0;
}
