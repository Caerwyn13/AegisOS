#include "e1000.h"
#include "paging.h"
#include "pit.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_VENDOR_INTEL   0x8086
#define PCI_DEVICE_82540EM 0x100E
#define E1000_MMIO_SIZE    0x6000
#define E1000_RING_SIZE    8
#define E1000_BUFFER_SIZE  2048
#define E1000_PACKET_MAX   1518

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_RAL    0x5400
#define REG_RAH    0x5404

#define CTRL_RESET 0x04000000
#define RCTL_ENABLE 0x00000002
#define RCTL_BROADCAST 0x00008000
#define RCTL_STRIP_CRC 0x04000000
#define TCTL_ENABLE 0x00000002
#define TCTL_PAD_SHORT 0x00000008
#define TX_CMD_EOP 0x01
#define TX_CMD_IFCS 0x02
#define TX_CMD_RS 0x08
#define DESC_DONE 0x01

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    volatile uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    volatile uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} e1000_tx_desc_t;

static volatile uint32_t *registers;
static uint8_t device_mac[E1000_MAC_LENGTH];
static e1000_rx_desc_t rx_ring[E1000_RING_SIZE] __attribute__((aligned(16)));
static e1000_tx_desc_t tx_ring[E1000_RING_SIZE] __attribute__((aligned(16)));
static uint8_t rx_buffers[E1000_RING_SIZE][E1000_BUFFER_SIZE]
    __attribute__((aligned(16)));
static uint8_t tx_buffers[E1000_RING_SIZE][E1000_BUFFER_SIZE]
    __attribute__((aligned(16)));
static uint32_t rx_index;
static uint32_t tx_index;
static int initialized;

static void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t function,
                         uint8_t offset) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t function,
                      uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static int find_device(uint32_t *bar_out, uint8_t *bus_out,
                       uint8_t *slot_out, uint8_t *function_out) {
    for (uint8_t bus = 0; bus < 1; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint32_t id = pci_read(bus, slot, function, 0);
                if ((id & 0xFFFF) == 0xFFFF)
                    break;
                if ((id & 0xFFFF) != PCI_VENDOR_INTEL ||
                    (id >> 16) != PCI_DEVICE_82540EM)
                    continue;

                uint32_t bar = pci_read(bus, slot, function, 0x10);
                if ((bar & 1) || (((bar >> 1) & 3) == 2))
                    return -1;

                uint32_t base = bar & ~0xFu;
                if (base == 0)
                    return -1;

                *bar_out = base;
                *bus_out = bus;
                *slot_out = slot;
                *function_out = function;
                return 0;
            }
        }
    }
    return -1;
}

static uint32_t reg_read(uint32_t offset) {
    return registers[offset / sizeof(uint32_t)];
}

static void reg_write(uint32_t offset, uint32_t value) {
    registers[offset / sizeof(uint32_t)] = value;
}

int e1000_init(uint8_t mac[E1000_MAC_LENGTH]) {
    uint32_t bar;
    uint8_t bus, slot, function;
    if (find_device(&bar, &bus, &slot, &function) != 0)
        return -1;

    uint32_t map_start = bar & ~(PAGE_SIZE - 1);
    uint32_t map_end = (bar + E1000_MMIO_SIZE + PAGE_SIZE - 1) &
                       ~(PAGE_SIZE - 1);
    for (uint32_t address = map_start; address < map_end; address += PAGE_SIZE)
        paging_map(address, address, PAGE_PRESENT | PAGE_RW);

    registers = (volatile uint32_t *)bar;

    uint32_t command = pci_read(bus, slot, function, 0x04);
    command |= 0x00000006;
    pci_write(bus, slot, function, 0x04, command);

    reg_write(REG_IMC, 0xFFFFFFFF);
    reg_write(REG_CTRL, reg_read(REG_CTRL) | CTRL_RESET);
    for (volatile uint32_t delay = 0; delay < 100000; delay++)
        __asm__ volatile ("pause");
    reg_write(REG_IMC, 0xFFFFFFFF);

    uint32_t ral = reg_read(REG_RAL);
    uint32_t rah = reg_read(REG_RAH);
    if (!(rah & 0x80000000))
        return -1;

    device_mac[0] = ral & 0xFF;
    device_mac[1] = (ral >> 8) & 0xFF;
    device_mac[2] = (ral >> 16) & 0xFF;
    device_mac[3] = (ral >> 24) & 0xFF;
    device_mac[4] = rah & 0xFF;
    device_mac[5] = (rah >> 8) & 0xFF;

    for (uint32_t i = 0; i < E1000_RING_SIZE; i++) {
        rx_ring[i].address = (uint32_t)rx_buffers[i];
        rx_ring[i].length = 0;
        rx_ring[i].checksum = 0;
        rx_ring[i].status = 0;
        rx_ring[i].errors = 0;
        rx_ring[i].special = 0;

        tx_ring[i].address = (uint32_t)tx_buffers[i];
        tx_ring[i].length = 0;
        tx_ring[i].checksum_offset = 0;
        tx_ring[i].command = 0;
        tx_ring[i].status = DESC_DONE;
        tx_ring[i].checksum_start = 0;
        tx_ring[i].special = 0;
    }

    reg_write(REG_RDBAL, (uint32_t)rx_ring);
    reg_write(REG_RDBAH, 0);
    reg_write(REG_RDLEN, sizeof(rx_ring));
    reg_write(REG_RDH, 0);
    reg_write(REG_RDT, E1000_RING_SIZE - 1);
    reg_write(REG_RCTL, RCTL_ENABLE | RCTL_BROADCAST | RCTL_STRIP_CRC);

    reg_write(REG_TDBAL, (uint32_t)tx_ring);
    reg_write(REG_TDBAH, 0);
    reg_write(REG_TDLEN, sizeof(tx_ring));
    reg_write(REG_TDH, 0);
    reg_write(REG_TDT, 0);
    reg_write(REG_TIPG, 0x0060200A);
    reg_write(REG_TCTL, TCTL_ENABLE | TCTL_PAD_SHORT | 0x00040100 | 0x10);

    rx_index = 0;
    tx_index = 0;
    for (uint32_t i = 0; i < E1000_MAC_LENGTH; i++)
        mac[i] = device_mac[i];
    initialized = 1;
    return 0;
}

int e1000_send(const uint8_t *packet, uint16_t length) {
    if (!initialized || !packet || length == 0 ||
        length > E1000_PACKET_MAX || length > E1000_BUFFER_SIZE)
        return -1;

    e1000_tx_desc_t *desc = &tx_ring[tx_index];
    uint32_t timeout = pit_ticks() + 1000;
    while (!(desc->status & DESC_DONE)) {
        if ((int32_t)(pit_ticks() - timeout) >= 0)
            return -1;
    }

    for (uint16_t i = 0; i < length; i++)
        tx_buffers[tx_index][i] = packet[i];
    desc->length = length;
    desc->command = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    desc->status = 0;
    __asm__ volatile ("" : : : "memory");

    tx_index = (tx_index + 1) % E1000_RING_SIZE;
    reg_write(REG_TDT, tx_index);
    return 0;
}

int e1000_receive(uint8_t *packet, uint16_t capacity) {
    if (!initialized || !packet || capacity == 0)
        return -1;

    e1000_rx_desc_t *desc = &rx_ring[rx_index];
    if (!(desc->status & DESC_DONE))
        return 0;

    int received = desc->length;
    if (desc->errors || received > capacity)
        received = -1;
    else {
        for (int i = 0; i < received; i++)
            packet[i] = rx_buffers[rx_index][i];
    }

    desc->status = 0;
    desc->length = 0;
    __asm__ volatile ("" : : : "memory");
    reg_write(REG_RDT, rx_index);
    rx_index = (rx_index + 1) % E1000_RING_SIZE;
    return received;
}
