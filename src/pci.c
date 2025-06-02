#include <csmwrap.h>
#include <io.h>
#include <pci.h>
#include <printf.h>

#include <uacpi/namespace.h>
#include <uacpi/resources.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>

#define PCI_OFFSET_MASK (~UINT64_C(3))

#define pci_read8(address, offset) ({ \
    /* Figure out which byte we want to read by AND-ing the offset with 0x3 (0b11) */ \
    /*/ This gives us a value between 0 and 3 (inclusive) */ \
    uint32_t byte_index = (offset) & 0x3; \
    uint32_t dword = pci_read((address), (offset) & PCI_OFFSET_MASK); \
    /* Shift the read dword right by `byte_index * 8` bits, that moves the byte */ \
    /* we are interested in to the least significant position */ \
    (uint8_t)((dword >> (byte_index * 8)) & 0xFF); \
})

#define pci_read16(address, offset) ({ \
    /* Figure out which word we want to read by AND-ing the offset with 0x2 (0b10) */ \
    /* This gives us a value of 0 or 2 which we then use to select the right word */ \
    uint32_t word_index = (offset) & 0x2; \
    uint32_t dword = pci_read((address), (offset) & PCI_OFFSET_MASK); \
    /* Shift the read dword right by `word_index * 8` bits, that moves the word */ \
    /* we are interested in to the least significant position */ \
    (uint16_t)((dword >> (word_index * 8)) & 0xFFFF); \
})

// No bit magic required here as that case matches the granularity of pci_read
#define pci_read32(address, offset) pci_read((address), (offset))

#define pci_write8(address, offset, value) ({ \
    uint32_t byte_index = (offset) & 0x3; \
    uint32_t dword = pci_read((address), (offset) & PCI_OFFSET_MASK); \
    /* First, we create a mask which will mask out the bits we are modifying */ \
    /* by shifting 0xFF left by `byte_index * 8` and the inverting it, then */ \
    /* we mask the read dword with that mask and finally we OR in the new values */ \
    /* shifted by `byte_index * 8` which places it at the right spot in the dword */ \
    uint32_t new_dword = (dword & ~(0xFF << (byte_index * 8))) | (((value) & 0xFF) << (byte_index * 8)); \
    pci_write((address), (offset) & PCI_OFFSET_MASK, new_dword); \
})

#define pci_write16(address, offset, value) ({ \
    uint32_t byte_index = (offset) & 0x2; \
    uint32_t dword = pci_read((address), (offset) & PCI_OFFSET_MASK); \
    /* This works similarly, except we use 0xFFFF because we work with a word */ \
    /* here instead of a single byte, and the `word_index` can either be 0 or 2 */ \
    uint32_t new_dword = (dword & ~(0xFFFF << (byte_index * 8))) | (((value) & 0xFFFF) << (byte_index * 8)); \
    pci_write((address), (offset) & PCI_OFFSET_MASK, new_dword); \
})

// And here, again, not bit fiddling required as this matches the granularity of pci_write
#define pci_write32(address, offset, value) pci_write((address), (offset), (value))

typedef uint32_t(*pci_read_t)(uacpi_pci_address *address, uint32_t offset);
typedef void    (*pci_write_t)(uacpi_pci_address *address, uint32_t offset, uint32_t value);

static uint32_t pci_read_pio(uacpi_pci_address *address, uint32_t offset)
{
    return pciConfigReadDWord(address->bus, address->device, address->function, offset);
}

static void pci_write_pio(uacpi_pci_address *address, uint32_t offset, uint32_t value)
{
    pciConfigWriteDWord(address->bus, address->device, address->function, offset, value);  
}

// TODO: PCI ECAM

static pci_read_t pci_read = pci_read_pio;
static pci_write_t pci_write = pci_write_pio;

static struct pci_root_bus **root_buses;
static size_t root_bus_count;

static bool add_bus(struct pci_root_bus *bus)
{
    struct pci_root_bus **buses;
    if (gBS->AllocatePool(EfiLoaderData, sizeof(struct pci_bus *) * (root_bus_count + 1), (void *)&buses) != EFI_SUCCESS) {
        printf("Failed to allocate memory for PCI root buses\n");
        return false;
    }

    buses[root_bus_count] = bus;
    
    if(root_buses != NULL) {
        memcpy(buses, root_buses, sizeof(struct pci_root_bus *) * root_bus_count);
        gBS->FreePool(root_buses);
    }

    root_bus_count += 1;
    root_buses = buses;

    return true;
}

static bool add_range(struct pci_root_bus *bus, uint64_t base, uint64_t length)
{
    struct address_range *ranges;
    if (gBS->AllocatePool(EfiLoaderData, sizeof(struct address_range) * (bus->range_count + 1), (void *)&ranges) != EFI_SUCCESS) {
        printf("Failed to allocate memory for PCI root bus address ranges\n");
        return false;
    }

    ranges[bus->range_count].base = base;
    ranges[bus->range_count].length = length;

    if(bus->ranges != NULL) {
        memcpy(ranges, bus->ranges, sizeof(struct address_range) * bus->range_count);
        gBS->FreePool(bus->ranges);
    }

    bus->range_count += 1;
    bus->ranges = ranges;

    return true;
}

static bool add_device(struct pci_root_bus *bus, struct pci_device *device)
{
    struct pci_device **devices;
    if (gBS->AllocatePool(EfiLoaderData, sizeof(struct pci_device *) * (bus->device_count + 1), (void *)&devices) != EFI_SUCCESS) {
        printf("Failed to allocate memory for PCI root bus devices\n");
        return false;
    }

    devices[bus->device_count] = device;

    if(bus->devices != NULL) {
        memcpy(devices, bus->devices, sizeof(struct pci_device *) * bus->device_count);
        gBS->FreePool(bus->devices);
    }

    bus->device_count += 1;
    bus->devices = devices;

    return true;
}

static bool add_bar(struct pci_root_bus *bus, struct pci_bar *bar)
{
    struct pci_bar **bars;
    if (gBS->AllocatePool(EfiLoaderData, sizeof(struct pci_bar *) * (bus->bar_count + 1), (void *)&bars) != EFI_SUCCESS) {
        printf("Failed to allocate memory for PCI root bus bars\n");
        return false;
    }

    bars[bus->bar_count] = bar;

    if(bus->bars != NULL) {
        memcpy(bars, bus->bars, sizeof(struct pci_bar *) * bus->bar_count);
        gBS->FreePool(bus->bars);
    }

    bus->bar_count += 1;
    bus->bars = bars;

    return true;
}

static bool scan_device_bars(struct pci_device *device)
{
    uacpi_pci_address address;
    address.segment = device->root_bus->segment;
    address.bus = device->root_bus->bus;
    address.device = device->slot;
    address.function = device->function;

    for (uint8_t bar = 0; bar < 6; ) {
        uint32_t bar_offset = 0x10 + bar * 4;
        uint32_t bar_value = pci_read32(&address, bar_offset);

        bool is_memory = false;
        bool is_64bit = false;

        // IO bars have bit 0 set
        if (!(bar_value & 0x1)) {
            // Memory bar layout is as follows:
            // - bit 0: always 0
            // - bit 1-2: bar type (0 is 32-bit, 1 is reserved, 2 is 64-bit)
            // - bit 3: prefetchable
            // - bit 4-31: base address

            is_memory = true;

            // Check the bar type to figure out whether it's a 64-bit bar
            if (((bar_value >> 1) & 0x3) == 0x2)
                is_64bit = true;
        }

        // Mask out the flag bits to get the bar address
        uint64_t base = bar_value & 0xFFFFFFF0;

        // If the bar is 64-bit then read the next bar's base address
        // and OR that into our current bar's base address - 64-bit bars
        // are made up of two consecutive bars to form a 64-bit address
        if (bar != 5 && is_64bit) {
            uint32_t next_bar = pci_read32(&address, bar_offset + 0x4);
            base |= (uint64_t)(next_bar & 0xFFFFFFF0) << 32;
        }

        // Disable bus master, memory and IO decoding to prevent the device
        // from mistakenly responding to our PCI config space accesses
        uint8_t cmd = pci_read8(&address, 0x4);
        uint8_t new_cmd = cmd;

        new_cmd &= ~(1 << 0); // IO space decoding
        new_cmd &= ~(1 << 1); // Memory space decoding
        new_cmd &= ~(1 << 2); // Bus master

        pci_write8(&address, 0x4, new_cmd);

        // Discover the bar length
        pci_write32(&address, bar_offset, 0xFFFFFFFF);
        uint32_t response = pci_read32(&address, bar_offset);
        pci_write32(&address, bar_offset, bar_value);
        uint64_t length = ~(response & 0xFFFFFFF0) + 1;

        if (bar != 5 && is_64bit) {
            uint32_t next_bar = pci_read32(&address, bar_offset + 0x4);
            pci_write32(&address, bar_offset + 0x4, 0xFFFFFFFF);
            uint32_t response = pci_read32(&address, bar_offset + 0x4);
            pci_write32(&address, bar_offset + 0x4, next_bar);
            length += ~(response & 0xFFFFFFF0) + 1;
        }

        // Restore command register
        pci_write8(&address, 0x4, cmd);

        if (base != 0 && is_memory) {
            struct pci_bar *bar_info;
            if (gBS->AllocatePool(EfiLoaderData, sizeof(struct pci_bar), (void *)&bar_info) != EFI_SUCCESS) {
                printf("Failed to allocate memory for PCI bar info\n");
                return false;
            }

            bar_info->device = device;
            bar_info->bar_number = bar;
            bar_info->range.base = base;
            bar_info->range.length = length;

            if (!add_bar(device->root_bus, bar_info))
                return false;
        }

        if (is_64bit)
            bar += 2;
        else
            bar += 1;
    }

    return true;
}

static bool check_function(struct pci_root_bus *bus, uacpi_pci_address *address)
{
    uint8_t subclass = pci_read16(address, 0xA);
    uint8_t class = (subclass >> 8) & 0xFF;

    subclass &= 0xFF;

    if (class == 0x6 && subclass == 0x4) {
        printf("TODO: handle pci-pci bridges\n");
        // uint8_t secondary_bus = pciConfigReadByte(bus, device, func, 0x19);
        // check_bus(secondary_bus);
        return true;
    }

    struct pci_device *device;
    if (gBS->AllocatePool(EfiLoaderData, sizeof(struct pci_device), (void *)&device) != EFI_SUCCESS) {
        printf("Failed to allocate memory for PCI device info\n");
        return false;
    }

    device->root_bus = bus;
    device->slot = address->device;
    device->function = address->function;
    device->type = PCI_DEVICE_REGULAR;

    if (!add_device(bus, device))
        return false;

    if (!scan_device_bars(device))
        return false;

    return true;
}

static bool check_device(struct pci_root_bus *bus, uacpi_pci_address *address)
{
    uint16_t vendor_id = pci_read16(address, 0x0);

    // No device on this slot, return
    if (vendor_id == 0xFFFF)
        return true;

    if (!check_function(bus, address))
        return false;

    // Check if device is multi-function
    uint8_t header_type = pci_read8(address, 0xE);
    if (!(header_type & 0x80))
        return true;

    for (uint8_t func = 1; func < 8; func++) {
        uacpi_pci_address func_addr = *address;
        func_addr.function = func;

        vendor_id = pci_read16(&func_addr, 0x0);
        if (vendor_id == 0xFFFF)
            continue;
        
        if (!check_function(bus, &func_addr))
            return false;
    }

    return true;
}

static bool discover_devices_on_bus(struct pci_root_bus *bus)
{
    for (uint8_t device = 0; device < 32; device++) {
        uacpi_pci_address address;
        address.segment = bus->segment;
        address.bus = bus->bus;
        address.device = device;
        address.function = 0;

        if (!check_device(bus, &address))
            return false;
    }

    return true;
}

static uacpi_iteration_decision discover_pci_root_bus(void *user, uacpi_namespace_node *node, uacpi_u32 node_depth)
{
    (void)node_depth;

    struct pci_root_bus *root_bus = NULL;
    
    uacpi_resources *resources = NULL;
    uacpi_iteration_decision decision = UACPI_ITERATION_DECISION_CONTINUE;
    uacpi_status status = UACPI_STATUS_OK;

    status = uacpi_get_current_resources(node, &resources);
    if (status != UACPI_STATUS_OK) {
        printf("Failed to get node resources: %s\n", uacpi_status_to_string(status));
        goto cleanup;
    }

    if (gBS->AllocatePool(EfiLoaderData, sizeof(struct pci_root_bus), (void *)&root_bus) != EFI_SUCCESS)
        goto cleanup;

    uint64_t segment, bus_number;

    uacpi_eval_simple_integer(node, "_SEG", &segment);
    uacpi_eval_simple_integer(node, "_BBN", &bus_number);

    root_bus->segment = segment;
    root_bus->bus = bus_number;

    root_bus->range_count = 0;
    root_bus->ranges = NULL;

    root_bus->device_count = 0;
    root_bus->devices = NULL;

    root_bus->bar_count = 0;
    root_bus->bars = NULL;

    uacpi_resource *res = resources->entries;
    while ((void *)res < (void *)resources->entries + resources->length)
    {
        if (res->type == UACPI_RESOURCE_TYPE_END_TAG)
            break;

        switch (res->type) {
        case UACPI_RESOURCE_TYPE_IO:
        case UACPI_RESOURCE_TYPE_FIXED_IO:
            // We don't care about IO regions
            break;
        case UACPI_RESOURCE_TYPE_ADDRESS16:
            if (res->address16.common.type != UACPI_RANGE_MEMORY || res->address16.address_length < 0x1000)
                break;
            if (!add_range(root_bus, res->address16.minimum, res->address16.address_length))
                goto cleanup;
            break;
        case UACPI_RESOURCE_TYPE_ADDRESS32:
            if (res->address32.common.type != UACPI_RANGE_MEMORY || res->address32.address_length < 0x1000)
                break;
            if (!add_range(root_bus, res->address32.minimum, res->address32.address_length))
                goto cleanup;
            break;
        case UACPI_RESOURCE_TYPE_ADDRESS64:
            if (res->address64.common.type != UACPI_RANGE_MEMORY || res->address64.address_length < 0x1000)
                break;
            if (!add_range(root_bus, res->address64.minimum, res->address64.address_length))
                goto cleanup;
            break;
        default:
            printf("Unknown PCI root bus resource type %u\n", res->type);
            break;
        }

        res = UACPI_NEXT_RESOURCE(res);
    }

    if (!discover_devices_on_bus(root_bus))
        goto cleanup;

    if (!add_bus(root_bus))
        goto cleanup;

    goto out;

cleanup:
    decision = UACPI_ITERATION_DECISION_BREAK;

    if (root_bus != NULL)
        pci_free_root_bus(root_bus);

    if (resources != NULL)
        uacpi_free_resources(resources);

out:
    *(uacpi_status *)user = status;
    return decision;
}

bool pci_initialize(void)
{
    uacpi_status status;
    uacpi_status iter_status = UACPI_STATUS_OK;

    status = uacpi_find_devices_at(
        uacpi_namespace_get_predefined(UACPI_PREDEFINED_NAMESPACE_SB),
        (const char *[]){"PNP0A03", "PNP0A08", NULL}, discover_pci_root_bus, &iter_status);

    if (iter_status != UACPI_STATUS_OK)
        status = iter_status;

    if (status != UACPI_STATUS_OK) {
        printf("uACPI find devices failed: %s\n", uacpi_status_to_string(status));
        return false;
    }

    printf("discovered %zu root buses\n", root_bus_count);

    for (size_t i = 0; i < root_bus_count; i++) {
        struct pci_root_bus *bus = root_buses[i];

        printf("  root bus %zu (%zu ranges, %zu devices, %zu bars):\n", i, bus->range_count, bus->device_count, bus->bar_count);

        for (size_t j = 0; j < bus->range_count; j++) {
            struct address_range *range = &bus->ranges[j];

            printf("    %zu: range 0x%llx..0x%llx\n", j, range->base, range->base + range->length - 1);
        }

        for (size_t j = 0; j < bus->device_count; j++) {
            struct pci_device *device = bus->devices[j];

            printf("    %zu: device at %04d:%02d.%02d.%02d\n", j,
                bus->segment, bus->bus, device->slot, device->function);
        }

        for (size_t j = 0; j < bus->bar_count; j++) {
            struct pci_bar *bar = bus->bars[j];

            printf("    %zu: bar%zu for %04d:%02d.%02d.%02d at 0x%llx..0x%llx\n", j,
                bar->bar_number, bus->segment, bus->bus, bar->device->slot, bar->device->function,
                bar->range.base, bar->range.base + bar->range.length - 1);
        }
    }

    return true;
}

void pci_free_root_bus(struct pci_root_bus *bus)
{
}

bool pci_insert_bar(struct pci_bar *bar)
{
    return false;
}

void pci_remove_bar(struct pci_bar *bar)
{
}

// Implementation of uACPI kernel APIs for reading/writing PCI config space

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8 *value)
{
    *value = pci_read8((uacpi_pci_address *)device, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16 *value)
{
    *value = pci_read16((uacpi_pci_address *)device, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32 *value)
{
    *value = pci_read32((uacpi_pci_address *)device, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value)
{
    pci_write8((uacpi_pci_address *)device, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value)
{
    pci_write16((uacpi_pci_address *)device, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value)
{
    pci_write32((uacpi_pci_address *)device, offset, value);
    return UACPI_STATUS_OK;
}
