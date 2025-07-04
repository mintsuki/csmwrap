#ifndef _CSM_WRAP_H
#define _CSM_WRAP_H

#include <efi.h>
#include <edk2/Acpi.h>
#include <edk2/LegacyBios.h>
#include <edk2/Coreboot.h>
#include <edk2/E820.h>
#include <edk2/Pci.h>
#include <libc.h>
#include <x86thunk.h>

extern EFI_SYSTEM_TABLE *gST;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_RUNTIME_SERVICES *gRT;
extern EFI_TIME gTimeAtBoot;

enum csmwrap_video_type {
    CSMWRAP_VIDEO_NONE,
    CSMWRAP_VIDEO_OPROM,
    CSMWRAP_VIDEO_SEAVGABIOS,
    CSMWRAP_VIDEO_FALLBACK,
};

struct csmwrap_priv {
    uint8_t *csm_bin;

    EFI_COMPATIBILITY16_TABLE *csm_efi_table;
    uintptr_t csm_bin_base;
    struct low_stub *low_stub;

    /* VGA stuff */
    enum csmwrap_video_type video_type;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_HANDLE gop_handle;
    EFI_PCI_IO_PROTOCOL *vga_pci_io;
    uint8_t vga_pci_bus;
    uint8_t vga_pci_devfn;
    struct cb_framebuffer cb_fb;
};

extern struct csmwrap_priv priv;

extern int unlock_bios_region();
extern int build_coreboot_table(struct csmwrap_priv *priv);
bool acpi_init(struct csmwrap_priv *priv);
bool acpi_full_init(void);
void acpi_prepare_exitbs(void);
int build_e820_map(struct csmwrap_priv *priv, EFI_MEMORY_DESCRIPTOR *memory_map, UINTN memory_map_size, UINTN descriptor_size);
int apply_intel_platform_workarounds(void);


static inline int
efi_guidcmp (EFI_GUID left, EFI_GUID right)
{
	return memcmp(&left, &right, sizeof (EFI_GUID));
}


#define E820_MAX_ENTRIES 128

#pragma pack(1)
struct low_stub {
    LOW_MEMORY_THUNK thunk;

    EFI_TO_COMPATIBILITY16_INIT_TABLE init_table;
    EFI_TO_COMPATIBILITY16_BOOT_TABLE boot_table;
    EFI_DISPATCH_OPROM_TABLE vga_oprom_table;

    /* E820 memory map */
    int e820_entries;
    EFI_E820_ENTRY64 e820_map[E820_MAX_ENTRIES];
};
#pragma pack()

/* Memory map information */
/* In low memory */
#define CB_TABLE_START  0x00000500
#define CONVEN_START    0x00007E00
/* We may have some stack here */
#define LOW_STUB_BASE   0x00020000
/* Thunk + PMM */
#define CONVEN_END      0x00080000
#define EBDA_BASE       CONVEN_END
#define VGABIOS_START   0x000C0000
#define VGABIOS_END     0x000C8000
#define BIOSROM_START   VGABIOS_END
#define BIOSROM_END     0x00100000
/* End of low 1MiB */
#define HIPMM_SIZE      0x400000 /* Allocated on runtime, can be anywhere in 32bit */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifdef ALIGN
#  undef ALIGN
#endif
#define ALIGN(x, a)             __ALIGN_MASK(x, (__typeof__(x))(a)-1UL)
#define __ALIGN_MASK(x, mask)   (((x)+(mask))&~(mask))
#define ALIGN_UP(x, a)          ALIGN((x), (a))
#define ALIGN_DOWN(x, a)        ((x) & ~((__typeof__(x))(a)-1UL))
#define IS_ALIGNED(x, a)        (((x) & ((__typeof__(x))(a)-1UL)) == 0)

#ifdef ACCESS_PAGE0_CODE
#  undef ACCESS_PAGE0_CODE
#endif

#define ACCESS_PAGE0_CODE(statements)                           \
  do {                                                          \
    statements;                                                 \
                                                                \
  } while (FALSE)

#endif
