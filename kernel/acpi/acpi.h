#ifndef ACPI_H
#define ACPI_H

enum {
    ACPI_REQUEST_ISSUED = 1,
    ACPI_ERR_RSDP = -1,
    ACPI_ERR_RSDT = -2,
    ACPI_ERR_FADT = -3,
    ACPI_ERR_DSDT = -4,
    ACPI_ERR_S5 = -5,
    ACPI_ERR_PM1 = -6,
    ACPI_ERR_ENABLE = -7,
    ACPI_ERR_RESET = -8
};

int acpi_shutdown(void);
int acpi_reboot(void);

#endif
