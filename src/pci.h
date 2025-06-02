#ifndef PCI_H
#define PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct address_range {
    uint64_t base;
    uint64_t length;
};

enum pci_device_type {
    PCI_DEVICE_REGULAR,
    PCI_DEVICE_BRIDGE,
};

struct pci_device {
    // Type of the device
    enum pci_device_type type;

    // The root bus that this device is on
    struct pci_root_bus *root_bus;

    // Address of the device on the bus
    uint8_t slot;
    uint8_t function;
};

struct pci_bridge {
    struct pci_device dev;

    // Bus number
    uint8_t bus;

    // List of devices associated with this bridge
    size_t device_count;
    struct pci_device *devices;
};

struct pci_bar {
    // The PCI device that this bar belongs to
    struct pci_device *device;

    // The bar number in context of the device
    uint8_t bar_number;

    // Base address and size of the bar
    struct address_range range;
};

struct pci_root_bus {
    uint32_t segment;
    uint8_t bus;

    // Sorted list of address ranges this root bus decodes
    size_t range_count;
    struct address_range *ranges;

    // List of devices associated with this root bus
    size_t device_count;
    struct pci_device **devices;

    // Sorted list of allocated bars associated with this root bus
    size_t bar_count;
    struct pci_bar **bars;
};

// Discover PCI root buses and devices behind them.
bool pci_initialize(void);

// Free a PCI root bus and all data associated with it.
void pci_free_root_bus(struct pci_root_bus *bus);

// Insert bar bottom up in some of the ranges of the associated root bus
// structure.
bool pci_insert_bar(struct pci_bar *bar);

// Remove the bar from the associated root bus.
// The structure is modified to reflect that by setting the base address to 0.
void pci_remove_bar(struct pci_bar *bar);

#endif
