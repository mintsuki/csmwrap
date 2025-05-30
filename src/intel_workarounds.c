#include <efi.h>
#include "csmwrap.h"

#include "io.h"

#define PCI_DEVICE_NUMBER_PCH_P2SB                 31
#define PCI_FUNCTION_NUMBER_PCH_P2SB               1

#define	SBREG_BAR		0x10
#define SBREG_BARH      0x14

#define PID_ITSS        0xC4

#define R_PCH_PCR_ITSS_ITSSPRC                0x3300          ///< ITSS Power Reduction Control
#define B_PCH_PCR_ITSS_ITSSPRC_PGCBDCGE       (1 << 4)            ///< PGCB Dynamic Clock Gating Enable
#define B_PCH_PCR_ITSS_ITSSPRC_HPETDCGE       (1 << 3)            ///< HPET Dynamic Clock Gating Enable
#define B_PCH_PCR_ITSS_ITSSPRC_8254CGE        (1 << 2)            ///< 8254 Static Clock Gating Enable
#define B_PCH_PCR_ITSS_ITSSPRC_IOSFICGE       (1 << 1)            ///< IOSF-Sideband Interface Clock Gating Enable
#define B_PCH_PCR_ITSS_ITSSPRC_ITSSCGE        (1 << 0)            ///< ITSS Clock Gate Enable

#define PCH_PCR_ADDRESS(Base, Pid, Offset)    ((void *)(Base | (UINT32) (((Offset) & 0x0F0000) << 8) | ((UINT8)(Pid) << 16) | (UINT16) ((Offset) & 0xFFFF)))

#define PORT_PIT_COUNTER0      0x0040
#define PORT_PIT_COUNTER1      0x0041
#define PORT_PIT_COUNTER2      0x0042
#define PORT_PIT_MODE          0x0043
#define PORT_PS2_CTRLB         0x0061

// Bits for PORT_PIT_MODE
#define PM_SEL_TIMER0   (0<<6)
#define PM_SEL_TIMER1   (1<<6)
#define PM_SEL_TIMER2   (2<<6)
#define PM_SEL_READBACK (3<<6)
#define PM_ACCESS_LATCH  (0<<4)
#define PM_ACCESS_LOBYTE (1<<4)
#define PM_ACCESS_HIBYTE (2<<4)
#define PM_ACCESS_WORD   (3<<4)
#define PM_MODE0 (0<<1)
#define PM_MODE1 (1<<1)
#define PM_MODE2 (2<<1)
#define PM_MODE3 (3<<1)
#define PM_MODE4 (4<<1)
#define PM_MODE5 (5<<1)
#define PM_CNT_BINARY (0<<0)
#define PM_CNT_BCD    (1<<0)
#define PM_READ_COUNTER0 (1<<1)
#define PM_READ_COUNTER1 (1<<2)
#define PM_READ_COUNTER2 (1<<3)
#define PM_READ_STATUSVALUE (0<<4)
#define PM_READ_VALUE       (1<<4)
#define PM_READ_STATUS      (2<<4)

#define R_P2SB_CFG_P2SBC                      0x000000e0U      ///< P2SB Control
                                                               /* P2SB general configuration register
                                                                */
#define B_P2SB_CFG_P2SBC_HIDE                 (1 << 8)         ///< P2SB Hide Bit

static int pit_8254cge_workaround(void)
{
    uint32_t reg;
    unsigned long base;
    bool p2sb_hide = false;
    int pch_pci_bus = 0;

    reg = pciConfigReadDWord(pch_pci_bus, PCI_DEVICE_NUMBER_PCH_P2SB,
                             PCI_FUNCTION_NUMBER_PCH_P2SB,
                             0x0);

    /* P2SB maybe hidden, try unhide it first */
    if ((reg & 0xFFFF) == 0xffff) {
        reg = pciConfigReadDWord(pch_pci_bus, PCI_DEVICE_NUMBER_PCH_P2SB,
                                 PCI_FUNCTION_NUMBER_PCH_P2SB,
                                 R_P2SB_CFG_P2SBC);
        reg &= ~B_P2SB_CFG_P2SBC_HIDE;
        pciConfigWriteDWord(pch_pci_bus, PCI_DEVICE_NUMBER_PCH_P2SB,
                            PCI_FUNCTION_NUMBER_PCH_P2SB,
                            R_P2SB_CFG_P2SBC, reg);
        p2sb_hide = true;
    }

    reg = pciConfigReadDWord(pch_pci_bus, PCI_DEVICE_NUMBER_PCH_P2SB,
                              PCI_FUNCTION_NUMBER_PCH_P2SB,
                              0x0);

    if ((reg & 0xFFFF) != 0x8086) {
        printf("No P2SB found, proceed to PIT test\n");
        goto test_pit;
    }

    reg = pciConfigReadDWord(pch_pci_bus, PCI_DEVICE_NUMBER_PCH_P2SB,
                              PCI_FUNCTION_NUMBER_PCH_P2SB,
                              SBREG_BAR);
    base = reg & ~0x0F;

    reg = pciConfigReadDWord(pch_pci_bus, PCI_DEVICE_NUMBER_PCH_P2SB,
                              PCI_FUNCTION_NUMBER_PCH_P2SB,
                              SBREG_BARH);
#ifdef __LP64__
    base |= ((uint64_t)reg & 0xFFFFFFFF) << 32;
#else
    if (reg) {
        printf("Invalid P2SB BARH\n");
        goto test_pit;
    }
#endif

    /* FIXME: Validate base */
    reg = readl(PCH_PCR_ADDRESS(base, PID_ITSS, R_PCH_PCR_ITSS_ITSSPRC));
    printf("ITSSPRC = %x, ITSSPRC.8254CGE= %x\n", reg, !!(reg & B_PCH_PCR_ITSS_ITSSPRC_8254CGE));
    /* Disable 8254CGE */
    reg &= ~B_PCH_PCR_ITSS_ITSSPRC_8254CGE;
    writel(PCH_PCR_ADDRESS(base, PID_ITSS, R_PCH_PCR_ITSS_ITSSPRC), reg);

    /* Hide P2SB again */
    if (p2sb_hide) {
        reg = pciConfigReadDWord(pch_pci_bus, PCI_DEVICE_NUMBER_PCH_P2SB,
                                 PCI_FUNCTION_NUMBER_PCH_P2SB,
                                 R_P2SB_CFG_P2SBC);
        reg |= B_P2SB_CFG_P2SBC_HIDE;
        pciConfigWriteDWord(pch_pci_bus, PCI_DEVICE_NUMBER_PCH_P2SB,
                            PCI_FUNCTION_NUMBER_PCH_P2SB,
                            R_P2SB_CFG_P2SBC, reg);
    }

test_pit:
    /* Lets hope we will not BOOM UEFI with this */
    outb(PORT_PIT_MODE, PM_SEL_READBACK | PM_READ_VALUE | PM_READ_COUNTER0);
    uint16_t v1 = inb(PORT_PIT_COUNTER0) | (inb(PORT_PIT_COUNTER0) << 8);

    gBS->Stall(1000);
    outb(PORT_PIT_MODE, PM_SEL_READBACK | PM_READ_VALUE | PM_READ_COUNTER0);
    uint16_t v2 = inb(PORT_PIT_COUNTER0) | (inb(PORT_PIT_COUNTER0) << 8);
    if (v1 == v2) {
        printf("PIT test failed, not counting!\n");
        return -1;
    }

    return 0;
}

int apply_intel_platform_workarounds(void)
{
    uint16_t vendor_id;

    vendor_id = pciConfigReadWord(0, 0, 0, 0x0);

    if (vendor_id != 0x8086) {
        return 0;
    }

    pit_8254cge_workaround();

    return 0;
}
