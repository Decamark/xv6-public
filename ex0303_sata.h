

// https://wiki.osdev.org/PCI#Configuration_Space_Access_Mechanism_#1
// https://github.com/coreboot/seabios/blob/master/src/hw/pcidevice.c#L168
char pci_config_readb(uint bus, uchar slot, uchar func, uchar offset) {
    uint address;
    uint lbus  = (uint)bus;
    uint lslot = (uint)slot;
    uint lfunc = (uint)func;
    uchar tmp = 0;
  
    // Create configuration address as per Figure 1
    address = (uint)((lbus  << 16) |
                     (lslot << 11) |
                     (lfunc << 8)  | 
                     (offset & 0xFC) | 
                     ((uint)0x80000000)
                    );
  
    // Write out the address
    outl(0xCF8, address);
    // Read in the data
    // (offset & 2) * 8) = 0 will choose the first word of the 32-bit register
    tmp = (uchar)((inl(0xCFC) >> ((offset & 2) * 8)) & 0xFFFF);
    return tmp;
}

// https://wiki.osdev.org/PCI#Configuration_Space_Access_Mechanism_#1
// https://github.com/coreboot/seabios/blob/master/src/hw/pcidevice.c#L168
ushort pci_config_readw(uint bus, uchar slot, uchar func, uchar offset) {
    uint address;
    uint lbus  = (uint)bus;
    uint lslot = (uint)slot;
    uint lfunc = (uint)func;
    ushort tmp = 0;
  
    // Create configuration address as per Figure 1
    address = (uint)((lbus << 16) |
                     (lslot << 11) |
                     (lfunc << 8) | 
                     (offset & 0xFC) | 
                     ((uint)0x80000000)
                    );
    // Write out the address
    outl(0xCF8, address);

    // Read in the data
    // (offset & 2) * 8) = 0 will choose the first word of the 32-bit register
    // 0 to 3
    tmp = (ushort)((inl(0xCFC) >> ((offset & 2) * 8)) & 0xFFFF);
    return tmp;
}

// https://github.com/coreboot/seabios/blob/master/src/hw/pci.c#L44
// https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/pci/pci_io.c#L30writel
void pci_config_writel(uint bus, uchar slot, uchar func, uchar offset, uint val)
{
    uint address;
    uint lbus  = (uint)bus;
    uint lslot = (uint)slot;
    uint lfunc = (uint)func;
    ushort tmp = 0;
  
    // Create configuration address as per Figure 1
    address = (uint)((1 << 31)       |
                     (lbus << 16)    |
                     (lslot << 11)   |
                     (lfunc << 8)    | 
                     (offset & 0xFC)
                    );
    // Write out the address
    outl(0xCF8, address);

    outl(0xCFC, val);
}

// https://wiki.osdev.org/PCI#Configuration_Space_Access_Mechanism_#1
// https://github.com/coreboot/seabios/blob/master/src/hw/pcidevice.c#L168
uint pci_config_readl(uint bus, uchar slot, uchar func, uchar offset) {
    uint address;
    uint lbus  = (uint)bus;
    uint lslot = (uint)slot;
    uint lfunc = (uint)func;
    uint tmp = 0;
  
    // Create configuration address as per Figure 1
    address = (uint)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | ((uint)0x80000000));
  
    // Write out the address
    outl(0xCF8, address);
    // Read in the data
    // (offset & 2) * 8) = 0 will choose the first word of the 32-bit register
    // tmp = (uint)((inl(0xCFC) >> ((offset & 2) * 8)) & 0xFFFFFFFFFFFF);
    tmp = inl(0xCFC);
    return tmp;
}

void printBuses()
{
  for (uint bus = 0; bus < 256; bus++) {
    for (uint device = 0; device < 32; device++) {
      for (uint function = 0; function < 8; function++) {
      if (pci_config_readw(bus, device, function, 0x00) == 0xffff) continue;

      cprintf("Bus: 0x%x, device: 0x%x, function: 0x%x\n", bus, device, function);

      cprintf("Vendor ID: 0x%x\n",   pci_config_readw(bus, device, function, 0x00));
      cprintf("Device ID: 0x%x\n",   pci_config_readw(bus, device, function, 0x02));
      cprintf("Status: 0x%x\n",      pci_config_readw(bus, device, function, 0x06));
      cprintf("Class code: 0x%x\n",  pci_config_readl(bus, device, function, 0x0b));
      cprintf("Header type: 0x%x\n", pci_config_readw(bus, device, function, 0x10));

      cprintf("\n");
      }
    }
  }
}

// Command list of each port, accessed by HBA_PORT.clb
typedef struct tagHBA_CMD_HEADER
{
    // DW0
    uchar  cfl:5;      // Command FIS length in DWORDS, 2 ~ 16
    uchar  a:1;        // ATAPI
    uchar  w:1;        // Write, 1: H2D, 0: D2H
    uchar  p:1;        // Prefetchable
 
    uchar  r:1;        // Reset
    uchar  b:1;        // BIST
    uchar  c:1;        // Clear busy upon R_OK
    uchar  rsv0:1;     // Reserved
    uchar  pmp:4;      // Port multiplier port
 
    ushort prdtl;      // Physical region descriptor table length in entries
 
    // DW1
    volatile
    uint prdbc;      // Physical region descriptor byte count transferred
 
    // DW2, 3
    uint ctba;       // Command table descriptor base address
    uint ctbau;      // Command table descriptor base address upper 32 bits
 
    // DW4 - 7
    uint rsv1[4];    // Reserved
} HBA_CMD_HEADER;

// Stores data information corresponding to Command FIS at the top of the Command Table
typedef struct tagHBA_PRDT_ENTRY
{
    uint dba;      // Data base address
    uint dbau;     // Data base address upper 32 bits
    uint rsv0;     // Reserved
 
    // DW3
    uint dbc:22;   // Byte count, 4M max
    uint rsv1:9;   // Reserved
    uint i:1;      // Interrupt on completion
} HBA_PRDT_ENTRY;

// Command table storing Command FIS
typedef struct tagHBA_CMD_TBL
{
    // 0x00
    uchar  cfis[64];       // Command FIS
 
    // 0x40
    uchar  acmd[16];       // ATAPI command, 12 or 16 bytes
 
    // 0x50
    uchar  rsv[48];        // Reserved
 
    // 0x80
    HBA_PRDT_ENTRY prdt_entry[1];  // Physical region descriptor table entries, 0 ~ 65535
} HBA_CMD_TBL;

// Information of each port
typedef volatile struct tagHBA_PORT
{
    uint clb;       // 0x00, command list base address, 1K-byte aligned
    uint clbu;      // 0x04, command list base address upper 32 bits
    uint fb;        // 0x08, FIS base address, 256-byte aligned
    uint fbu;       // 0x0C, FIS base address upper 32 bits
    uint is;        // 0x10, interrupt status
    uint ie;        // 0x14, interrupt enable
    uint cmd;       // 0x18, command and status
    uint rsv0;      // 0x1C, Reserved
    uint tfd;       // 0x20, task file data
    uint sig;       // 0x24, signature
    uint ssts;      // 0x28, SATA status (SCR0:SStatus)
    uint sctl;      // 0x2C, SATA control (SCR2:SControl)
    uint serr;      // 0x30, SATA error (SCR1:SError)
    uint sact;      // 0x34, SATA active (SCR3:SActive)
    uint ci;        // 0x38, command issue
    uint sntf;      // 0x3C, SATA notification (SCR4:SNotification)
    uint fbs;       // 0x40, FIS-based switch control
    uint rsv1[11];  // 0x44 ~ 0x6F, Reserved
    uint vendor[4]; // 0x70 ~ 0x7F, vendor specific
} HBA_PORT;

#define HBA_PxCMD_CR  (1 << 15)
#define HBA_PxCMD_FR  (1 << 14)
#define HBA_PxCMD_FRE (1 << 4)
#define HBA_PxCMD_SUD (1 << 1)
#define HBA_PxCMD_ST  1

#define HBA_PxCMD_LIST_ON          (1 << 15) /* cmd list DMA engine running */
#define HBA_PxCMD_FIS_ON           (1 << 14) /* FIS DMA engine running */
#define HBA_PxCMD_FIS_RX           (1 << 4) /* Enable FIS receive DMA engine */
#define HBA_PxCMD_CLO              (1 << 3) /* Command list override */
#define HBA_PxCMD_POWER_ON         (1 << 2) /* Power up device */
#define HBA_PxCMD_SPIN_UP          (1 << 1) /* Spin up device */
#define HBA_PxCMD_START            (1 << 0) /* Enable port DMA engine */

#define HBA_PxCMD_ICC_MASK         (0xf << 28) /* i/f ICC state mask */
#define HBA_PxCMD_ICC_ACTIVE       (0x1 << 28) /* Put i/f in active state */
#define HBA_PxCMD_ICC_PARTIAL      (0x2 << 28) /* Put i/f in partial state */
#define HBA_PxCMD_ICC_SLUMBER      (0x6 << 28) /* Put i/f in slumber state */

// AHCI information 0xFD0C0000
typedef volatile struct tagHBA_MEM
{
    // 0x00 - 0x2B, Generic Host Control
    uint cap;       // 0x00, Host capability
    uint ghc;       // 0x04, Global host control
    uint is;        // 0x08, Interrupt status
    uint pi;        // 0x0C, Port implemented
    uint vs;        // 0x10, Version
    uint ccc_ctl;   // 0x14, Command completion coalescing control
    uint ccc_pts;   // 0x18, Command completion coalescing ports
    uint em_loc;    // 0x1C, Enclosure management location
    uint em_ctl;    // 0x20, Enclosure management control
    uint cap2;      // 0x24, Host capabilities extended
    uint bohc;      // 0x28, BIOS/OS handoff control and status
 
    // 0x2C - 0x9F, Reserved
    uchar  rsv[0xA0-0x2C];
 
    // 0xA0 - 0xFF, Vendor specific registers
    uint pctrl;
    uint pcfg;
    uint ppcfg;
    uint pp2c;
    // 0xb0
    uint pp3c;
    uint pp4c;
    uint pp5c;
    uint axicc;
    // 0xc0
    uint paxic;
    uint axipc;
    uint ptc;
    uint pts;
    // 0xd0
    uint plc;
    uint plc1;
    uint plc2;
    uint pls;
    // 0xe0
    uint pls1;
    uint pcmdc;
    uint ppcs;
    uint ams;
    // 0xf0
    uint tcr;
    uint vendor[3];
 
    // 0x100 - 0x10FF, Port control registers
    HBA_PORT ports[2];   // 1 ~ 32
} HBA_MEM;

#define HBA_PORT_IPM_ACTIVE  1
#define HBA_PORT_DET_PRESENT 3
#define HBA_RESET            0x01            // HBA reset bit in Global Host Control register
#define AHCI_ENABLE          0x80000000      // AHCI Enable bit in Global Host Control register
#define AHCI_IE              0x00000002      // Interrupt Enable bit in Global Host Control register 

typedef struct {
    ushort data[256];
} IDENTIFY_DEVICE_DATA;

typedef enum
{
	FIS_TYPE_REG_H2D	= 0x27,	// Register FIS - host to device
	FIS_TYPE_REG_D2H	= 0x34,	// Register FIS - device to host
	FIS_TYPE_DMA_ACT	= 0x39,	// DMA activate FIS - device to host
	FIS_TYPE_DMA_SETUP	= 0x41,	// DMA setup FIS - bidirectional
	FIS_TYPE_DATA		= 0x46,	// Data FIS - bidirectional
	FIS_TYPE_BIST		= 0x58,	// BIST activate FIS - bidirectional
	FIS_TYPE_PIO_SETUP	= 0x5F,	// PIO setup FIS - device to host
	FIS_TYPE_DEV_BITS	= 0xA1,	// Set device bits FIS - device to host
} FIS_TYPE;

typedef enum 
{
  AHCI_ATA_CMD_IDENTIFY     = 0xEC,
  // ATA_CMD_READ_DMA     = 0xC8,
  AHCI_ATA_CMD_READ_DMA_EX  = 0x25,
  // ATA_CMD_WRITE_DMA    = 0xCA,
  AHCI_ATA_CMD_WRITE_DMA_EX = 0x35
} FIS_COMMAND;


typedef struct tagFIS_REG_H2D
{
	// DWORD 0
	uchar  fis_type;	// FIS_TYPE_REG_H2D
 
	uchar  pmport:4;	// Port multiplier
	uchar  rsv0:3;		// Reserved
	uchar  c:1;		// 1: Command, 0: Control
 
	uchar  command;	// Command register
	uchar  featurel;	// Feature register, 7:0
 
	// DWORD 1
	uchar  lba0;		// LBA low register, 7:0
	uchar  lba1;		// LBA mid register, 15:8
	uchar  lba2;		// LBA high register, 23:16
	uchar  device;		// Device register
 
	// DWORD 2
	uchar  lba3;		// LBA register, 31:24
	uchar  lba4;		// LBA register, 39:32
	uchar  lba5;		// LBA register, 47:40
	uchar  featureh;	// Feature register, 15:8
 
	// DWORD 3
	uchar  countl;		// Count register, 7:0
	uchar  counth;		// Count register, 15:8
	uchar  icc;		// Isochronous command completion
	uchar  control;	// Control register
 
	// DWORD 4
	uchar  rsv1[4];	// Reserved
} FIS_REG_H2D;


typedef struct tagFIS_REG_D2H
{
	// DWORD 0
	uchar  fis_type;    // FIS_TYPE_REG_D2H
 
	uchar  pmport:4;    // Port multiplier
	uchar  rsv0:2;      // Reserved
	uchar  i:1;         // Interrupt bit
	uchar  rsv1:1;      // Reserved
 
	uchar  status;      // Status register
	uchar  error;       // Error register
 
	// DWORD 1
	uchar  lba0;        // LBA low register, 7:0
	uchar  lba1;        // LBA mid register, 15:8
	uchar  lba2;        // LBA high register, 23:16
	uchar  device;      // Device register
 
	// DWORD 2
	uchar  lba3;        // LBA register, 31:24
	uchar  lba4;        // LBA register, 39:32
	uchar  lba5;        // LBA register, 47:40
	uchar  rsv2;        // Reserved
 
	// DWORD 3
	uchar  countl;      // Count register, 7:0
	uchar  counth;      // Count register, 15:8
	uchar  rsv3[2];     // Reserved
 
	// DWORD 4
	uchar  rsv4[4];     // Reserved
} FIS_REG_D2H;







struct sata_cmd_fis {
    uchar reg;
    uchar pmp_type;
    uchar command;
    uchar feature;

    uchar lba_low;
    uchar lba_mid;
    uchar lba_high;
    uchar device;

    uchar lba_low2;
    uchar lba_mid2;
    uchar lba_high2;
    uchar feature2;

    uchar sector_count;
    uchar sector_count2;
    uchar res_1;
    uchar control;

    uchar res_2[64 - 16];
};

struct ahci_fis_s {
    uchar dsfis[0x1c];  /* dma setup */
    uchar res_1[0x04];
    uchar psfis[0x14];  /* pio setup */
    uchar res_2[0x0c];
    uchar rfis[0x14];   /* d2h register */
    uchar res_3[0x04];
    uchar sdbfis[0x08]; /* set device bits */
    uchar ufis[0x40];   /* unknown */
    uchar res_4[0x60];
};

struct ahci_cmd_s {
    struct sata_cmd_fis fis;
    uchar atapi[0x20];
    uchar res[0x20];
    struct {
        uint base;
        uint baseu;
        uint res;
        uint flags;
    } prdt[];
};

/* command list */
struct ahci_list_s {
    uint flags;
    uint bytes;
    uint base;
    uint baseu;
    uint res[4];
};

static uint ahci_port_to_ctrl(uint pnr, uint port_reg)
{
    uint ctrl_reg = 0x100;
    ctrl_reg += pnr * 0x80;
    ctrl_reg += port_reg;
    return ctrl_reg;
}
