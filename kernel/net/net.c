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
#define IPV4_PROTOCOL_TCP 6
#define ARP_PACKET_SIZE 28
#define PACKET_BUFFER_SIZE 1518
#define ARP_TIMEOUT_MS 2000
#define PING_TIMEOUT_MS 1000
#define PING_INTERVAL_MS 1000
#define TCP_TIMEOUT_MS 1000
#define TCP_RETRIES 3
#define TCP_HEADER_SIZE 20
#define HTTP_TIMEOUT_MS 8000
#define HTTP_PORT 80

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

// DHCP Settings
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_FIXED_SIZE  240
#define DHCP_TIMEOUT_MS  4000

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

// DNS Settings
#define DNS_SERVER_PORT 53
#define DNS_TIMEOUT_MS  4000

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

static uint16_t tcp_checksum(uint32_t source, uint32_t destination,
                             const uint8_t *segment, uint16_t length) {
    uint32_t sum = 0;
    sum += (source >> 16) & 0xFFFF;
    sum += source & 0xFFFF;
    sum += (destination >> 16) & 0xFFFF;
    sum += destination & 0xFFFF;
    sum += IPV4_PROTOCOL_TCP;
    sum += length;

    while (length > 1) {
        sum += read_be16(segment);
        segment += 2;
        length -= 2;
    }
    if (length)
        sum += (uint16_t)segment[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void copy_mac(uint8_t *destination, const uint8_t *source) {
    for (uint32_t i = 0; i < 6; i++)
        destination[i] = source[i];
}

static int send_dhcp_message(uint8_t message_type, uint32_t xid, uint32_t requested_ip, uint32_t server_ip) {
    static const uint8_t broadcast[6] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };

    memset(packet_buffer, 0, PACKET_BUFFER_SIZE);

    ethernet_header_t *ethernet = (ethernet_header_t *)packet_buffer;
    copy_mac(ethernet->destination, broadcast);
    copy_mac(ethernet->source, interface_info.mac);
    ethernet->type = __builtin_bswap16(ETHERNET_TYPE_IPV4);

    uint8_t *ip = packet_buffer + ETHERNET_HEADER_SIZE;
    ip[0] = 0x45;
    ip[8] = 64;
    ip[9] = 17;
    write_be32(ip + 12, 0);
    write_be32(ip + 16, 0xFFFFFFFF);

    uint8_t *udp = ip + IPV4_HEADER_SIZE;
    write_be16(udp, DHCP_CLIENT_PORT);
    write_be16(udp + 2, DHCP_SERVER_PORT);

    uint8_t *bootp = udp + 8;
    bootp[0] = 1;
    bootp[1] = 1;
    bootp[2] = 6;
    write_be32(bootp + 4, xid);
    write_be16(bootp + 10, 0x8000);
    copy_mac(bootp + 28, interface_info.mac);

    bootp[236] = 99;
    bootp[237] = 130;
    bootp[238] = 83;
    bootp[239] = 99;

    uint32_t options_length = 0;
    uint8_t *options = bootp + DHCP_FIXED_SIZE;

    options[options_length++] = 53;
    options[options_length++] = 1;
    options[options_length++] = message_type;

    if (message_type == DHCP_REQUEST) {
        options[options_length++] = 50;
        options[options_length++] = 4;
        write_be32(options + options_length, requested_ip);
        options_length += 4;

        options[options_length++] = 54;
        options[options_length++] = 4;
        write_be32(options + options_length, server_ip);
        options_length += 4;
    }

    options[options_length++] = 55;
    options[options_length++] = 3;
    options[options_length++] = 1;
    options[options_length++] = 3;
    options[options_length++] = 6;
    options[options_length++] = 255;

    uint16_t bootp_length = DHCP_FIXED_SIZE + options_length;
    uint16_t udp_length = 8 + bootp_length;
    uint16_t ip_length = IPV4_HEADER_SIZE + udp_length;
    uint16_t frame_length = ETHERNET_HEADER_SIZE + ip_length;

    write_be16(udp + 4, udp_length);
    write_be16(ip + 2, ip_length);
    write_be16(ip + 10, checksum(ip, IPV4_HEADER_SIZE));

    return e1000_send(packet_buffer, frame_length);
}

static int wait_dhcp_reply(uint8_t expected_type, uint32_t xid,
                           uint32_t expected_server, uint32_t *offered_ip,
                           uint32_t *netmask, uint32_t *gateway,
                           uint32_t *dns_server, uint32_t *server_ip) {
    uint32_t start = pit_ticks();

    while ((uint32_t)(pit_ticks() - start) < DHCP_TIMEOUT_MS) {
        int frame_length = e1000_receive(packet_buffer, sizeof(packet_buffer));
        if (frame_length <= 0 ||
            frame_length < ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE)
            continue;

        if (read_be16(packet_buffer + 12) != ETHERNET_TYPE_IPV4)
            continue;

        const uint8_t *ip = packet_buffer + ETHERNET_HEADER_SIZE;
        uint32_t ip_header_length = (ip[0] & 0x0F) * 4;
        if ((ip[0] >> 4) != 4 ||
            ip_header_length < IPV4_HEADER_SIZE ||
            ip[9] != 17 ||
            (read_be16(ip + 6) & 0x3FFF) != 0 ||
            (uint32_t)frame_length <
                ETHERNET_HEADER_SIZE + ip_header_length + 8)
            continue;

        uint16_t ip_length = read_be16(ip + 2);
        if (ip_length < ip_header_length + 8 ||
            ip_length > frame_length - ETHERNET_HEADER_SIZE ||
            checksum(ip, ip_header_length) != 0)
            continue;

        const uint8_t *udp = ip + ip_header_length;
        uint16_t udp_length = read_be16(udp + 4);
        if (read_be16(udp) != DHCP_SERVER_PORT ||
            read_be16(udp + 2) != DHCP_CLIENT_PORT ||
            udp_length < 8 + DHCP_FIXED_SIZE ||
            udp_length > ip_length - ip_header_length)
            continue;

        const uint8_t *bootp = udp + 8;
        uint16_t bootp_length = udp_length - 8;
        if (bootp[0] != 2 || bootp[1] != 1 || bootp[2] != 6 ||
            read_be32(bootp + 4) != xid || bootp_length < DHCP_FIXED_SIZE ||
            bootp[236] != 99 || bootp[237] != 130 ||
            bootp[238] != 83 || bootp[239] != 99)
            continue;

        int matching_mac = 1;
        for (uint32_t i = 0; i < 6; i++) {
            if (bootp[28 + i] != interface_info.mac[i]) {
                matching_mac = 0;
                break;
            }
        }
        if (!matching_mac)
            continue;

        uint8_t reply_type = 0;
        uint32_t reply_netmask = 0;
        uint32_t reply_gateway = 0;
        uint32_t reply_server = 0;
        uint32_t reply_dns = 0;
        uint32_t option_index = DHCP_FIXED_SIZE;

        while (option_index < bootp_length) {
            uint8_t code = bootp[option_index++];
            if (code == 255)
                break;
            if (code == 0)
                continue;
            if (option_index >= bootp_length)
                break;

            uint8_t option_length = bootp[option_index++];
            if (option_length > bootp_length - option_index)
                break;

            const uint8_t *value = bootp + option_index;
            if (code == 53 && option_length == 1)
                reply_type = value[0];
            else if (code == 1 && option_length == 4)
                reply_netmask = read_be32(value);
            else if (code == 3 && option_length >= 4)
                reply_gateway = read_be32(value);
            else if (code == 54 && option_length == 4)
                reply_server = read_be32(value);
            else if (code == 6 && option_length >= 4)
                reply_dns = read_be32(value);
            option_index += option_length;
        }

        if (reply_type != expected_type)
            continue;
        if (expected_server && reply_server != expected_server)
            continue;

        *offered_ip = read_be32(bootp + 16);
        *netmask = reply_netmask;
        *gateway = reply_gateway;
        *server_ip = reply_server;
        *dns_server = reply_dns;
        return 0;
    }

    return -1;
}

static int dhcp_configure(void) {
    for (uint32_t attempt = 0; attempt < 3; attempt++) {
        uint32_t xid = (pit_ticks() << 16) ^
                       ((uint32_t)interface_info.mac[2] << 24) ^
                       ((uint32_t)interface_info.mac[4] << 8) ^
                       interface_info.mac[5];
        uint32_t offered_ip = 0;
        uint32_t netmask = 0;
        uint32_t gateway = 0;
        uint32_t server_ip = 0;
        uint32_t dns_server = 0;

        if (send_dhcp_message(DHCP_DISCOVER, xid, 0, 0) != 0)
            continue;

        if (wait_dhcp_reply(DHCP_OFFER, xid, 0, &offered_ip,
                            &netmask, &gateway, &dns_server, &server_ip) != 0 ||
            offered_ip == 0 || server_ip == 0)
            continue;

        if (send_dhcp_message(DHCP_REQUEST, xid, offered_ip, server_ip) != 0)
            continue;

        if (wait_dhcp_reply(DHCP_ACK, xid, server_ip, &offered_ip,
                            &netmask, &gateway, &dns_server, &server_ip) != 0 ||
            offered_ip == 0 || netmask == 0)
            continue;

        interface_info.ip_address = offered_ip;
        interface_info.netmask = netmask;
        interface_info.gateway = gateway;
        interface_info.dns_server = dns_server;
        return 0;
    }

    return -1;
}


void net_init(void) {
    memset(&interface_info, 0, sizeof(interface_info));

    if (e1000_init(interface_info.mac) != 0)
        return;

    if (dhcp_configure() != 0)
        return;

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

static int dns_skip_name(const uint8_t *message, uint32_t length,
                         uint32_t *offset) {
    uint32_t cursor = *offset;
    uint32_t resume = 0;
    uint32_t steps = 0;

    for (;;) {
        if (cursor >= length || ++steps > length)
            return -1;

        uint8_t label_length = message[cursor];
        if ((label_length & 0xC0) == 0xC0) {
            if (cursor + 1 >= length)
                return -1;

            uint32_t pointer =
                ((uint32_t)(label_length & 0x3F) << 8) | message[cursor + 1];
            if (pointer >= length)
                return -1;

            if (!resume)
                resume = cursor + 2;
            cursor = pointer;
            continue;
        }

        if (label_length & 0xC0)
            return -1;

        cursor++;
        if (label_length == 0) {
            *offset = resume ? resume : cursor;
            return 0;
        }

        if (label_length > 63 || label_length > length - cursor)
            return -1;

        cursor += label_length;
    }
}

int net_resolve_ipv4(const char *hostname, uint32_t *address) {
    if (!hostname || !address || !interface_info.available ||
        !interface_info.dns_server)
        return -1;

    uint32_t name_length = 0;
    uint32_t label_length = 0;
    const char *label = hostname;

    for (const char *p = hostname; ; p++) {
        if (*p == '.' || *p == '\0') {
            if (label_length == 0 || label_length > 63 ||
                *label == '-' || p[-1] == '-')
                return -1;

            name_length += label_length + 1;
            if (name_length > 254)
                return -1;

            if (*p == '\0')
                break;

            label = p + 1;
            label_length = 0;
            continue;
        }

        char c = *p;
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '-'))
            return -1;

        label_length++;
        if (label_length > 63)
            return -1;
    }

    uint32_t next_hop = route_ip(interface_info.dns_server);
    uint8_t destination_mac[6];
    if (resolve_mac(next_hop, destination_mac) != 0)
        return -2;

    memset(packet_buffer, 0, sizeof(packet_buffer));

    ethernet_header_t *ethernet = (ethernet_header_t *)packet_buffer;
    copy_mac(ethernet->destination, destination_mac);
    copy_mac(ethernet->source, interface_info.mac);
    ethernet->type = __builtin_bswap16(ETHERNET_TYPE_IPV4);

    uint8_t *ip = packet_buffer + ETHERNET_HEADER_SIZE;
    ip[0] = 0x45;
    ip[8] = 64;
    ip[9] = 17;
    write_be32(ip + 12, interface_info.ip_address);
    write_be32(ip + 16, interface_info.dns_server);

    uint8_t *udp = ip + IPV4_HEADER_SIZE;
    uint16_t identifier = next_identifier++;
    uint16_t source_port = 49152 + (identifier & 0x3FFF);
    write_be16(udp, source_port);
    write_be16(udp + 2, DNS_SERVER_PORT);

    uint8_t *dns = udp + 8;
    write_be16(dns, identifier);
    write_be16(dns + 2, 0x0100);
    write_be16(dns + 4, 1);

    uint8_t *qname = dns + 12;
    uint32_t qname_length = 0;
    label_length = 0;
    label = hostname;

    for (const char *p = hostname; ; p++) {
        if (*p == '.' || *p == '\0') {
            *qname++ = (uint8_t)label_length;
            for (const char *c = label; c < p; c++)
                *qname++ = (uint8_t)*c;
            qname_length += label_length + 1;

            if (*p == '\0')
                break;

            label = p + 1;
            label_length = 0;
            continue;
        }

        label_length++;
    }

    *qname++ = 0;
    qname_length++;
    write_be16(qname, 1);
    write_be16(qname + 2, 1);
    qname_length += 4;

    uint16_t dns_length = 12 + qname_length;
    uint16_t udp_length = 8 + dns_length;
    uint16_t ip_length = IPV4_HEADER_SIZE + udp_length;
    uint16_t frame_length = ETHERNET_HEADER_SIZE + ip_length;

    write_be16(udp + 4, udp_length);
    write_be16(ip + 2, ip_length);
    write_be16(ip + 10, checksum(ip, IPV4_HEADER_SIZE));

    if (e1000_send(packet_buffer, frame_length) != 0)
        return -3;

    uint32_t start = pit_ticks();
    while ((uint32_t)(pit_ticks() - start) < DNS_TIMEOUT_MS) {
        int frame_length_received =
            e1000_receive(packet_buffer, sizeof(packet_buffer));
        if (frame_length_received <
            ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE + 8 + 12)
            continue;

        if (read_be16(packet_buffer + 12) != ETHERNET_TYPE_IPV4)
            continue;

        const uint8_t *reply_ip = packet_buffer + ETHERNET_HEADER_SIZE;
        uint32_t ip_header_length = (reply_ip[0] & 0x0F) * 4;
        if ((reply_ip[0] >> 4) != 4 ||
            ip_header_length < IPV4_HEADER_SIZE ||
            reply_ip[9] != 17 ||
            (read_be16(reply_ip + 6) & 0x3FFF) != 0 ||
            (uint32_t)frame_length_received <
                ETHERNET_HEADER_SIZE + ip_header_length + 8)
            continue;

        uint16_t reply_ip_length = read_be16(reply_ip + 2);
        if (reply_ip_length < ip_header_length + 8 ||
            reply_ip_length >
                frame_length_received - ETHERNET_HEADER_SIZE ||
            checksum(reply_ip, ip_header_length) != 0 ||
            read_be32(reply_ip + 12) != interface_info.dns_server ||
            read_be32(reply_ip + 16) != interface_info.ip_address)
            continue;

        const uint8_t *reply_udp = reply_ip + ip_header_length;
        uint16_t reply_udp_length = read_be16(reply_udp + 4);
        if (read_be16(reply_udp) != DNS_SERVER_PORT ||
            read_be16(reply_udp + 2) != source_port ||
            reply_udp_length < 8 + 12 ||
            reply_udp_length > reply_ip_length - ip_header_length)
            continue;

        const uint8_t *reply_dns = reply_udp + 8;
        uint32_t reply_dns_length = reply_udp_length - 8;
        uint16_t flags = read_be16(reply_dns + 2);
        if (read_be16(reply_dns) != identifier ||
            !(flags & 0x8000) ||
            (flags & 0x7A00) != 0 ||
            read_be16(reply_dns + 4) != 1)
            continue;
        if (flags & 0x000F)
            return -4;

        uint32_t cursor = 12;
        if (dns_skip_name(reply_dns, reply_dns_length, &cursor) != 0 ||
            cursor + 4 > reply_dns_length ||
            read_be16(reply_dns + cursor) != 1 ||
            read_be16(reply_dns + cursor + 2) != 1)
            continue;
        cursor += 4;

        uint16_t answer_count = read_be16(reply_dns + 6);
        for (uint32_t i = 0; i < answer_count; i++) {
            if (dns_skip_name(reply_dns, reply_dns_length, &cursor) != 0 ||
                cursor + 10 > reply_dns_length)
                break;

            uint16_t type = read_be16(reply_dns + cursor);
            uint16_t class_code = read_be16(reply_dns + cursor + 2);
            uint16_t data_length = read_be16(reply_dns + cursor + 8);
            cursor += 10;

            if (data_length > reply_dns_length - cursor)
                break;

            if (type == 1 && class_code == 1 && data_length == 4) {
                *address = read_be32(reply_dns + cursor);
                return 0;
            }

            cursor += data_length;
        }

        return -4;
    }

    return -4;
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

typedef struct {
    uint32_t sequence;
    uint32_t acknowledgement;
    uint16_t payload_length;
    uint8_t flags;
} tcp_segment_info_t;

static int tcp_send_segment(uint32_t destination, const uint8_t *destination_mac,
                            uint16_t source_port, uint16_t destination_port,
                            uint32_t sequence, uint32_t acknowledgement,
                            uint8_t flags, const uint8_t *payload,
                            uint16_t payload_length) {
    const uint16_t tcp_length = TCP_HEADER_SIZE + payload_length;
    const uint16_t ip_length = IPV4_HEADER_SIZE + tcp_length;
    const uint16_t frame_length = ETHERNET_HEADER_SIZE + ip_length;
    memset(packet_buffer, 0, frame_length);

    ethernet_header_t *ethernet = (ethernet_header_t *)packet_buffer;
    copy_mac(ethernet->destination, destination_mac);
    copy_mac(ethernet->source, interface_info.mac);
    ethernet->type = __builtin_bswap16(ETHERNET_TYPE_IPV4);

    uint8_t *ip = packet_buffer + ETHERNET_HEADER_SIZE;
    ip[0] = 0x45;
    write_be16(ip + 2, ip_length);
    write_be16(ip + 4, next_identifier++);
    write_be16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = IPV4_PROTOCOL_TCP;
    write_be32(ip + 12, interface_info.ip_address);
    write_be32(ip + 16, destination);
    write_be16(ip + 10, checksum(ip, IPV4_HEADER_SIZE));

    uint8_t *tcp = ip + IPV4_HEADER_SIZE;
    write_be16(tcp, source_port);
    write_be16(tcp + 2, destination_port);
    write_be32(tcp + 4, sequence);
    write_be32(tcp + 8, acknowledgement);
    tcp[12] = 5 << 4;
    tcp[13] = flags;
    write_be16(tcp + 14, 4096);
    if (payload_length) {
        if (!payload || tcp_length + IPV4_HEADER_SIZE + ETHERNET_HEADER_SIZE >
                            PACKET_BUFFER_SIZE)
            return -1;
        memcpy(tcp + TCP_HEADER_SIZE, payload, payload_length);
    }
    write_be16(tcp + 16,
               tcp_checksum(interface_info.ip_address, destination,
                            tcp, tcp_length));

    return e1000_send(packet_buffer, frame_length);
}

static int tcp_receive_segment(uint32_t destination, uint16_t source_port,
                               uint16_t destination_port,
                               tcp_segment_info_t *segment,
                               uint8_t *payload, uint16_t payload_capacity) {
    int frame_length = e1000_receive(packet_buffer, sizeof(packet_buffer));
    if (frame_length <= 0 ||
        frame_length < ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE + TCP_HEADER_SIZE ||
        read_be16(packet_buffer + 12) != ETHERNET_TYPE_IPV4)
        return 0;

    const uint8_t *ip = packet_buffer + ETHERNET_HEADER_SIZE;
    uint32_t ip_header_length = (ip[0] & 0x0F) * 4;
    if ((ip[0] >> 4) != 4 ||
        ip_header_length < IPV4_HEADER_SIZE ||
        ip[9] != IPV4_PROTOCOL_TCP ||
        (read_be16(ip + 6) & 0x3FFF) != 0 ||
        (uint32_t)frame_length <
            ETHERNET_HEADER_SIZE + ip_header_length + TCP_HEADER_SIZE)
        return 0;

    uint16_t ip_length = read_be16(ip + 2);
    if (ip_length < ip_header_length + TCP_HEADER_SIZE ||
        ip_length > (uint32_t)frame_length - ETHERNET_HEADER_SIZE ||
        checksum(ip, ip_header_length) != 0 ||
        read_be32(ip + 12) != destination ||
        read_be32(ip + 16) != interface_info.ip_address)
        return 0;

    const uint8_t *tcp = ip + ip_header_length;
    uint32_t tcp_header_length = (tcp[12] >> 4) * 4;
    uint16_t tcp_length = ip_length - ip_header_length;
    if (tcp_header_length < TCP_HEADER_SIZE ||
        tcp_header_length > tcp_length ||
        read_be16(tcp) != destination_port ||
        read_be16(tcp + 2) != source_port ||
        tcp_checksum(destination, interface_info.ip_address,
                     tcp, tcp_length) != 0)
        return 0;

    segment->sequence = read_be32(tcp + 4);
    segment->acknowledgement = read_be32(tcp + 8);
    segment->payload_length = tcp_length - tcp_header_length;
    segment->flags = tcp[13];
    if (payload && payload_capacity) {
        uint16_t copy_length = segment->payload_length;
        if (copy_length > payload_capacity)
            copy_length = payload_capacity;
        memcpy(payload, tcp + tcp_header_length, copy_length);
    }
    return 1;
}

int net_tcp_connect_close(uint32_t destination, uint16_t port) {
    if (!interface_info.available || !destination || !port)
        return -1;

    uint32_t next_hop = route_ip(destination);
    uint8_t destination_mac[6];
    if (resolve_mac(next_hop, destination_mac) != 0)
        return -2;

    uint16_t identifier = next_identifier++;
    uint16_t source_port = 49152 + (identifier & 0x3FFF);
    uint32_t client_sequence = (pit_ticks() << 16) ^ identifier;
    uint32_t client_next = client_sequence + 1;
    tcp_segment_info_t segment;
    int connected = 0;

    for (uint32_t attempt = 0; attempt < TCP_RETRIES && !connected; attempt++) {
        if (tcp_send_segment(destination, destination_mac, source_port, port,
                             client_sequence, 0, TCP_FLAG_SYN, 0, 0) != 0)
            return -3;

        uint32_t start = pit_ticks();
        while ((uint32_t)(pit_ticks() - start) < TCP_TIMEOUT_MS) {
            if (!tcp_receive_segment(destination, source_port, port, &segment,
                                     0, 0))
                continue;
            if (segment.flags & TCP_FLAG_RST)
                return -3;
            if ((segment.flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
                    (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
                segment.acknowledgement == client_next) {
                uint32_t server_next = segment.sequence + 1 +
                                       segment.payload_length;
                if (tcp_send_segment(destination, destination_mac, source_port,
                                     port, client_next, server_next,
                                     TCP_FLAG_ACK, 0, 0) != 0)
                    return -3;
                segment.sequence = server_next;
                connected = 1;
                break;
            }
            pit_sleep(1);
        }
    }

    if (!connected)
        return -4;

    uint32_t server_next = segment.sequence;
    client_next++;

    for (uint32_t attempt = 0; attempt < TCP_RETRIES; attempt++) {
        if (tcp_send_segment(destination, destination_mac, source_port, port,
                             client_next - 1, server_next,
                             TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0) != 0)
            return -3;

        uint32_t start = pit_ticks();
        while ((uint32_t)(pit_ticks() - start) < TCP_TIMEOUT_MS) {
            if (!tcp_receive_segment(destination, source_port, port, &segment,
                                     0, 0))
                continue;
            if (segment.flags & TCP_FLAG_RST)
                return -3;
            if (!(segment.flags & TCP_FLAG_ACK) ||
                segment.acknowledgement != client_next ||
                segment.sequence != server_next) {
                pit_sleep(1);
                continue;
            }

            server_next += segment.payload_length;
            if (segment.flags & TCP_FLAG_FIN) {
                server_next++;
                if (tcp_send_segment(destination, destination_mac, source_port,
                                     port, client_next, server_next,
                                     TCP_FLAG_ACK, 0, 0) != 0)
                    return -3;
                return 0;
            }

            if (segment.payload_length &&
                tcp_send_segment(destination, destination_mac, source_port,
                                 port, client_next, server_next,
                                 TCP_FLAG_ACK, 0, 0) != 0)
                return -3;
            pit_sleep(1);
        }
    }

    (void)tcp_send_segment(destination, destination_mac, source_port, port,
                           client_next, server_next,
                           TCP_FLAG_RST | TCP_FLAG_ACK, 0, 0);
    return -5;
}

int net_http_get(uint32_t destination, const char *host, const char *path,
                 char *response, uint16_t response_capacity,
                 uint16_t *response_length, uint8_t *truncated) {
    if (!interface_info.available || !destination || !host || !*host ||
        !path || path[0] != '/' || !response || response_capacity == 0 ||
        !response_length || !truncated)
        return -1;

    uint32_t host_length = 0;
    for (const char *p = host; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-'))
            return -1;
        if (++host_length > 253)
            return -1;
    }

    uint32_t path_length = 0;
    for (const char *p = path; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '/' || c == '?' ||
              c == '&' || c == '=' || c == '%' || c == '-' ||
              c == '.' || c == '_' || c == '~' || c == ':' ||
              c == '@' || c == '!' || c == '$' || c == '\'' ||
              c == '(' || c == ')' || c == '*' || c == '+' ||
              c == ',' || c == ';'))
            return -1;
        if (++path_length > 200)
            return -1;
    }

    if (path_length == 0)
        return -1;

    char request[512];
    static const char request_prefix[] = "GET ";
    static const char request_middle[] = " HTTP/1.0\r\nHost: ";
    static const char request_suffix[] = "\r\nConnection: close\r\n\r\n";
    uint32_t prefix_length = sizeof(request_prefix) - 1;
    uint32_t middle_length = sizeof(request_middle) - 1;
    uint32_t suffix_length = sizeof(request_suffix) - 1;
    uint32_t request_length = prefix_length + path_length + middle_length +
                              host_length + suffix_length;
    if (request_length > sizeof(request))
        return -1;

    uint32_t cursor = 0;
    for (uint32_t i = 0; i < prefix_length; i++)
        request[cursor++] = request_prefix[i];
    for (uint32_t i = 0; i < path_length; i++)
        request[cursor++] = path[i];
    for (uint32_t i = 0; i < middle_length; i++)
        request[cursor++] = request_middle[i];
    for (uint32_t i = 0; i < host_length; i++)
        request[cursor++] = host[i];
    for (uint32_t i = 0; i < suffix_length; i++)
        request[cursor++] = request_suffix[i];

    *response_length = 0;
    *truncated = 0;
    response[0] = '\0';

    uint32_t next_hop = route_ip(destination);
    uint8_t destination_mac[6];
    if (resolve_mac(next_hop, destination_mac) != 0)
        return -2;

    uint16_t identifier = next_identifier++;
    uint16_t source_port = 49152 + (identifier & 0x3FFF);
    uint32_t client_sequence = (pit_ticks() << 16) ^ identifier;
    uint32_t client_next = client_sequence + 1;
    tcp_segment_info_t segment;
    int connected = 0;

    for (uint32_t attempt = 0; attempt < TCP_RETRIES && !connected; attempt++) {
        if (tcp_send_segment(destination, destination_mac, source_port, HTTP_PORT,
                             client_sequence, 0, TCP_FLAG_SYN, 0, 0) != 0)
            return -3;

        uint32_t start = pit_ticks();
        while ((uint32_t)(pit_ticks() - start) < TCP_TIMEOUT_MS) {
            if (!tcp_receive_segment(destination, source_port, HTTP_PORT,
                                     &segment, 0, 0))
                continue;
            if (segment.flags & TCP_FLAG_RST)
                return -3;
            if ((segment.flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
                    (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
                segment.acknowledgement == client_next) {
                uint32_t server_next = segment.sequence + 1 +
                                       segment.payload_length;
                if (tcp_send_segment(destination, destination_mac, source_port,
                                     HTTP_PORT, client_next, server_next,
                                     TCP_FLAG_ACK, 0, 0) != 0)
                    return -3;

                client_next += request_length;
                if (tcp_send_segment(destination, destination_mac, source_port,
                                     HTTP_PORT, client_next - request_length,
                                     server_next, TCP_FLAG_PSH | TCP_FLAG_ACK,
                                     (const uint8_t *)request,
                                     request_length) != 0)
                    return -3;
                segment.sequence = server_next;
                connected = 1;
                break;
            }
            pit_sleep(1);
        }
    }

    if (!connected)
        return -4;

    uint32_t server_next = segment.sequence;
    uint8_t incoming[PACKET_BUFFER_SIZE];
    uint32_t start = pit_ticks();
    int peer_closed = 0;
    while ((uint32_t)(pit_ticks() - start) < HTTP_TIMEOUT_MS) {
        if (!tcp_receive_segment(destination, source_port, HTTP_PORT, &segment,
                                 incoming, sizeof(incoming)))
            continue;
        if (segment.flags & TCP_FLAG_RST)
            return -5;
        if (!(segment.flags & TCP_FLAG_ACK) ||
            segment.acknowledgement != client_next ||
            segment.sequence != server_next) {
            pit_sleep(1);
            continue;
        }

        if (segment.payload_length) {
            uint32_t remaining = response_capacity - 1 - *response_length;
            uint32_t copied = segment.payload_length;
            if (copied > remaining) {
                copied = remaining;
                *truncated = 1;
            }
            for (uint32_t i = 0; i < copied; i++)
                response[*response_length + i] = (char)incoming[i];
            *response_length += copied;
            response[*response_length] = '\0';
            server_next += segment.payload_length;
        }

        if (segment.flags & TCP_FLAG_FIN) {
            server_next++;
            if (tcp_send_segment(destination, destination_mac, source_port,
                                 HTTP_PORT, client_next, server_next,
                                 TCP_FLAG_ACK, 0, 0) != 0)
                return -5;
            peer_closed = 1;
            break;
        }

        if (segment.payload_length &&
            tcp_send_segment(destination, destination_mac, source_port,
                             HTTP_PORT, client_next, server_next,
                             TCP_FLAG_ACK, 0, 0) != 0)
            return -5;
        pit_sleep(1);
    }

    if (!peer_closed)
        return -5;

    uint32_t close_sequence = client_next;
    client_next++;
    for (uint32_t attempt = 0; attempt < TCP_RETRIES; attempt++) {
        if (tcp_send_segment(destination, destination_mac, source_port,
                             HTTP_PORT, close_sequence, server_next,
                             TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0) != 0)
            return -5;

        start = pit_ticks();
        while ((uint32_t)(pit_ticks() - start) < TCP_TIMEOUT_MS) {
            if (!tcp_receive_segment(destination, source_port, HTTP_PORT,
                                     &segment, 0, 0))
                continue;
            if (segment.flags & TCP_FLAG_RST)
                return -5;
            if ((segment.flags & TCP_FLAG_ACK) &&
                segment.acknowledgement == client_next)
                return 0;
            pit_sleep(1);
        }
    }

    return -5;
}
