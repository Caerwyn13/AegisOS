#include "acpi.h"
#include "types.h"
#include "paging.h"
#include "heap.h"
#include "ports.h"
#include "string.h"

#define ACPI_TEMP_VIRT  0xFFC10000
#define ACPI_TABLE_MAX  (1024 * 1024)
#define ACPI_SLP_TYP    (7 << 10)
#define ACPI_SLP_EN     (1 << 13)
#define ACPI_SCI_EN     1
#define FADT_RESET_REG_OFFSET 116
#define FADT_RESET_VALUE_OFFSET 128

typedef struct __attribute__((packed)) {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
} acpi_gas_t;

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} rsdp_v1_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_table_header_t;

typedef struct __attribute__((packed)) {
    acpi_table_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint32_t pm2_control_block;
    uint32_t pm_timer_block;
    uint32_t gpe0_block;
    uint32_t gpe1_block;
    uint8_t pm1_event_length;
    uint8_t pm1_control_length;
} fadt_legacy_t;

typedef struct {
    uint8_t sleep_type_a;
    uint8_t sleep_type_b;
} acpi_s5_t;

static int read_physical(uint32_t address, void *destination, uint32_t size) {
    uint8_t *out = (uint8_t *)destination;

    if (size && address > 0xFFFFFFFFu - (size - 1))
        return -1;

    while (size) {
        uint32_t page = address & ~(PAGE_SIZE - 1);
        uint32_t offset = address & (PAGE_SIZE - 1);
        uint32_t chunk = PAGE_SIZE - offset;
        if (chunk > size)
            chunk = size;

        paging_map(ACPI_TEMP_VIRT, page, PAGE_PRESENT | PAGE_RW);
        if (!paging_get_phys(ACPI_TEMP_VIRT)) {
            paging_unmap(ACPI_TEMP_VIRT);
            return -1;
        }

        memcpy(out, (const void *)(ACPI_TEMP_VIRT + offset), chunk);
        paging_unmap(ACPI_TEMP_VIRT);
        address += chunk;
        out += chunk;
        size -= chunk;
    }

    return 0;
}

static int checksum_valid(const uint8_t *bytes, uint32_t length) {
    uint8_t sum = 0;

    for (uint32_t i = 0; i < length; i++)
        sum += bytes[i];

    return sum == 0;
}

static int bytes_equal(const uint8_t *left, const char *right, uint32_t length) {
    for (uint32_t i = 0; i < length; i++)
        if (left[i] != (uint8_t)right[i])
            return 0;

    return 1;
}

static int scan_rsdp(uint32_t start, uint32_t end, rsdp_v1_t *result) {
    start = (start + 15) & ~15u;

    for (uint32_t address = start;
         address <= end - sizeof(rsdp_v1_t);
         address += 16) {
        uint8_t bytes[sizeof(rsdp_v1_t)];

        if (read_physical(address, bytes, sizeof(bytes)) < 0)
            continue;

        if (!bytes_equal(bytes, "RSD PTR ", 8) ||
            !checksum_valid(bytes, sizeof(bytes)))
            continue;

        memcpy(result, bytes, sizeof(*result));
        return 0;
    }

    return -1;
}

static int find_rsdp(rsdp_v1_t *result) {
    uint16_t ebda_segment;
    uint32_t ebda_address;

    if (read_physical(0x40E, &ebda_segment, sizeof(ebda_segment)) == 0) {
        ebda_address = (uint32_t)ebda_segment << 4;
        if (ebda_address >= 0x80000 && ebda_address <= 0x9FC00 &&
            scan_rsdp(ebda_address, ebda_address + 1024, result) == 0)
            return 0;
    }

    return scan_rsdp(0xE0000, 0x100000, result);
}

static uint8_t *load_table(uint32_t address, const char *signature) {
    acpi_table_header_t header;

    if (!address || read_physical(address, &header, sizeof(header)) < 0)
        return 0;
    if (!bytes_equal((const uint8_t *)header.signature, signature, 4) ||
        header.length < sizeof(header) || header.length > ACPI_TABLE_MAX)
        return 0;

    uint8_t *table = (uint8_t *)kmalloc(header.length);
    if (!table)
        return 0;
    if (read_physical(address, table, header.length) < 0 ||
        !checksum_valid(table, header.length)) {
        kfree(table);
        return 0;
    }

    return table;
}

static uint8_t *find_fadt(void) {
    rsdp_v1_t rsdp;
    if (find_rsdp(&rsdp) < 0 || !rsdp.rsdt_address)
        return 0;

    uint8_t *rsdt = load_table(rsdp.rsdt_address, "RSDT");
    if (!rsdt)
        return 0;

    acpi_table_header_t *header = (acpi_table_header_t *)rsdt;
    uint32_t entries_size = header->length - sizeof(*header);
    if (entries_size % sizeof(uint32_t)) {
        kfree(rsdt);
        return 0;
    }

    uint8_t *fadt = 0;
    for (uint32_t offset = sizeof(*header); offset < header->length;
         offset += sizeof(uint32_t)) {
        uint32_t table_address = *(uint32_t *)(rsdt + offset);
        fadt = load_table(table_address, "FACP");
        if (fadt)
            break;
    }

    kfree(rsdt);
    return fadt;
}

static int aml_read_integer(const uint8_t *aml, uint32_t end,
                            uint32_t *offset, uint32_t *value) {
    if (*offset >= end)
        return -1;

    uint8_t opcode = aml[(*offset)++];
    uint32_t bytes;

    if (opcode == 0x00) {
        *value = 0;
        return 0;
    }
    if (opcode == 0x01) {
        *value = 1;
        return 0;
    }

    switch (opcode) {
        case 0x0A: bytes = 1; break;
        case 0x0B: bytes = 2; break;
        case 0x0C: bytes = 4; break;
        default: return -1;
    }

    if (bytes > end - *offset)
        return -1;

    *value = 0;
    for (uint32_t i = 0; i < bytes; i++)
        *value |= (uint32_t)aml[(*offset)++] << (i * 8);

    return 0;
}

static int aml_package_length(const uint8_t *aml, uint32_t end,
                              uint32_t *offset, uint32_t *length) {
    if (*offset >= end)
        return -1;

    uint8_t lead = aml[(*offset)++];
    uint32_t following = lead >> 6;

    if (following == 0) {
        *length = lead & 0x3F;
        return 0;
    }
    if (following > end - *offset)
        return -1;

    *length = lead & 0x0F;
    for (uint32_t i = 0; i < following; i++)
        *length |= (uint32_t)aml[(*offset)++] << (4 + i * 8);

    return 0;
}

static int find_s5(const uint8_t *dsdt, uint32_t length, acpi_s5_t *s5) {
    for (uint32_t i = sizeof(acpi_table_header_t); i + 7 <= length; i++) {
        if (dsdt[i] != 0x08 || !bytes_equal(dsdt + i + 1, "_S5_", 4))
            continue;

        uint32_t offset = i + 5;
        if (offset >= length || dsdt[offset++] != 0x12)
            continue;

        uint32_t package_offset = offset;
        uint32_t package_length;
        if (aml_package_length(dsdt, length, &offset, &package_length) < 0)
            continue;

        uint32_t package_end = package_offset + package_length;
        if (package_end > length || offset >= package_end)
            continue;

        uint8_t elements = dsdt[offset++];
        uint32_t type_a, type_b;
        if (elements < 2 ||
            aml_read_integer(dsdt, package_end, &offset, &type_a) < 0 ||
            aml_read_integer(dsdt, package_end, &offset, &type_b) < 0 ||
            type_a > 7 || type_b > 7)
            continue;

        s5->sleep_type_a = (uint8_t)type_a;
        s5->sleep_type_b = (uint8_t)type_b;
        return 0;
    }

    return -1;
}

static int enable_acpi(const fadt_legacy_t *fadt) {
    if (fadt->pm1a_control_block > 0xFFFF ||
        fadt->pm1_control_length < 2)
        return -1;

    uint16_t control_port = (uint16_t)fadt->pm1a_control_block;
    if (inw(control_port) & ACPI_SCI_EN)
        return 0;

    if (!fadt->smi_command || !fadt->acpi_enable ||
        fadt->smi_command > 0xFFFF)
        return -1;

    outb((uint16_t)fadt->smi_command, fadt->acpi_enable);

    for (volatile uint32_t timeout = 10000000; timeout; timeout--) {
        if (inw(control_port) & ACPI_SCI_EN)
            return 0;
    }

    return -1;
}

int acpi_shutdown(void) {
    uint8_t *fadt_table = find_fadt();
    if (!fadt_table)
        return ACPI_ERR_RSDP;

    fadt_legacy_t *fadt = (fadt_legacy_t *)fadt_table;
    if (fadt->header.length < sizeof(fadt_legacy_t)) {
        kfree(fadt_table);
        return ACPI_ERR_FADT;
    }

    uint32_t dsdt_address = fadt->dsdt;
    uint32_t pm1a = fadt->pm1a_control_block;
    uint32_t pm1b = fadt->pm1b_control_block;
    uint32_t pm1_length = fadt->pm1_control_length;

    if (!dsdt_address || enable_acpi(fadt) < 0) {
        kfree(fadt);
        return ACPI_ERR_ENABLE;
    }

    uint8_t *dsdt = load_table(dsdt_address, "DSDT");
    kfree(fadt);
    if (!dsdt)
        return ACPI_ERR_DSDT;

    acpi_s5_t s5;
    int found_s5 = find_s5(dsdt, ((acpi_table_header_t *)dsdt)->length, &s5);
    kfree(dsdt);
    if (found_s5 < 0)
        return ACPI_ERR_S5;

    if (!pm1a || pm1a > 0xFFFF || pm1_length < 2 ||
        (pm1b && pm1b > 0xFFFF))
        return ACPI_ERR_PM1;

    uint16_t value_a = inw((uint16_t)pm1a);
    value_a = (value_a & ~ACPI_SLP_TYP) |
              ((uint16_t)s5.sleep_type_a << 10) | ACPI_SLP_EN;
    outw((uint16_t)pm1a, value_a);

    if (pm1b) {
        uint16_t value_b = inw((uint16_t)pm1b);
        value_b = (value_b & ~ACPI_SLP_TYP) |
                  ((uint16_t)s5.sleep_type_b << 10) | ACPI_SLP_EN;
        outw((uint16_t)pm1b, value_b);
    }

    return ACPI_REQUEST_ISSUED;
}

int acpi_reboot(void) {
    uint8_t *fadt = find_fadt();
    if (!fadt)
        return ACPI_ERR_FADT;

    acpi_table_header_t *header = (acpi_table_header_t *)fadt;
    if (header->length < FADT_RESET_VALUE_OFFSET + 1) {
        kfree(fadt);
        return ACPI_ERR_RESET;
    }

    acpi_gas_t *reset_register = (acpi_gas_t *)(fadt + FADT_RESET_REG_OFFSET);
    uint8_t reset_value = fadt[FADT_RESET_VALUE_OFFSET];
    if (reset_register->address_space != 1 ||
        reset_register->bit_width != 8 ||
        reset_register->bit_offset != 0 ||
        reset_register->access_size > 1 ||
        reset_register->address > 0xFFFF) {
        kfree(fadt);
        return ACPI_ERR_RESET;
    }

    outb((uint16_t)reset_register->address, reset_value);
    kfree(fadt);
    return ACPI_REQUEST_ISSUED;
}
