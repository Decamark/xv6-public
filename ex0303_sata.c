// https://wiki.osdev.org/AHCI

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "x86.h"
#include "memlayout.h"
#include "ex0303_sata.h"

// submit ahci command + wait for result
// https://wiki.osdev.org/AHCI
// static int ahci_command(
//   uint pnr, struct sata_cmd_fis* cfis,
//   int iswrite, int isatapi,
//   void *buffer, uint bsize)
// {
//   // 4.2.3 Command Table
//   struct ahci_cmd_s* cmd = (struct ahci_cmd_s*)kalloc();

//   memmove(&cmd->fis, cfis, sizeof(struct sata_cmd_fis));

//   // H2D Register FIS format
//   cmd->fis.reg       = 0x27;
//   cmd->fis.pmp_type  = 1 << 7; /* cmd fis */

//   cmd->prdt[0].base  = (uint)buffer;
//   cmd->prdt[0].baseu = 0;
//   cmd->prdt[0].flags = 0xffffffff;

//   // Put the command at 0 of the command list
//   uint flags = ((1 << 16)                | /* one prd entry */
//                 (iswrite ? (1 << 6) : 0) |
//                 (isatapi ? (1 << 5) : 0) |
//                 (5 << 0)                   /* fis length (dwords) */
//               );
//   cmd_list[pnr][0].base   = (uint)(cmd);
//   cmd_list[pnr][0].baseu  = 0;
//   cmd_list[pnr][0].bytes  = 0;
//   cmd_list[pnr][0].flags  = flags;

//   uint intbits = *(uint*)(bar5 + ahci_port_to_ctrl(pnr, 0x10));
//   cprintf("is: 0x%x\n", intbits);
//   *(uint*)(bar5 + ahci_port_to_ctrl(pnr, 0x10)) = -1;

//   uint tf = *(uint*)(bar5 + ahci_port_to_ctrl(pnr, 0x10));
//   cprintf("tf: 0x%x\n", tf);

//   // Issue a command
//   // PxCI - Port x Command Issue
//   *(uint*)(bar5 + ahci_port_to_ctrl(pnr, 0x38)) = 1;

//   cprintf("ci: 0x%x\n", *(uint*)(bar5 + ahci_port_to_ctrl(pnr, 0x38)));

//   // Check the status
//   // for (;;) {
//     //   uint intbits = *(uint*)(bar5 + ahci_port_to_ctrl(pnr, 0x10));
//     //   cprintf("intbits: 0x%x\n", intbits);
//     // https://github.com/coreboot/seabios/blob/master/src/hw/ahci.c#L142
//     // 3.3.8 Offset 20h: PxTFD – Port x Task File Data
//     // uint tf = *(uint*)(bar5 + ahci_port_to_ctrl(pnr, 0x20));
//   // }

//   cprintf("is: 0x%x\n", *(uint*)(bar5 + ahci_port_to_ctrl(pnr, 0x10)));

//   uint status = *((uint*)&fis[pnr]->dsfis[2]);
//   cprintf("status: 0x%x\n", status);

//   uint error = *((uint*)&fis[pnr]->dsfis[3]);
//   cprintf("error:  0x%x\n", error);

//   // uint port_cmd = *(uint*)(bar5 + ahci_port_to_ctrl(pnr, 0x10));
//   // *(uint*)(bar5 + ahci_port_to_ctrl(pnr, 0x10)) = port_cmd | ~1;

//   return 0;
// }

HBA_MEM* bar5;

// Start command engine
void start_cmd(HBA_PORT *port)
{
  	while (port->cmd & HBA_PxCMD_CR)
		;

  cprintf("tfd: 0x%x\n", port->tfd);

  // Set FRE (bit4)
  // Enable FIS recv
	port->cmd |= HBA_PxCMD_FRE;

  cprintf("tfd: 0x%x\n", port->tfd);

  // spin up
  // https://github.com/coreboot/seabios/blob/master/src/hw/ahci.c#L447
  port->cmd &= ~HBA_PxCMD_ICC_MASK;
  port->cmd |= HBA_PxCMD_SPIN_UP | HBA_PxCMD_POWER_ON | HBA_PxCMD_ICC_ACTIVE;

//   for (uint timeout=0; timeout < 1000000; timeout++) {
//     if (!(port->tfd & (0x80 | 0x08))) {
//       goto startdev;
//     }
//   }
//   return;

// startdev:
}

// Stop command engine
void stop_cmd(HBA_PORT *port)
{
	// Clear ST (bit0)
	port->cmd &= ~HBA_PxCMD_ST;

	// Clear FRE (bit4)
	port->cmd &= ~HBA_PxCMD_FRE;

	// Wait until FR (bit14), CR (bit15) are cleared
	while(1)
	{
		if (port->cmd & HBA_PxCMD_FR)
			continue;
		if (port->cmd & HBA_PxCMD_CR)
			continue;
    if (port->cmd & HBA_PxCMD_FRE)
      continue;
    if (port->cmd & HBA_PxCMD_ST)
      continue;
		break;
	}

  // Disable interrupt
  port->ie = 0;
  port->is = port->is;

  port->is   = 0xFFFFFFFF;        // clear port interrupts
  port->serr = 0xFFFFFFFF;        // clear SATA errors
}

// Alloc CLB and FB
void ahci_port_alloc(HBA_PORT* port)
{
  // 4.2.2 Command List Structure
  uint clb   = kalloc();
  memset(clb, 0, 1024);
  cprintf("clb: 0x%x\n", clb);
  port->clb  = V2P(clb);
  port->clbu = 0;

  // Alloc 32 command tables
  HBA_CMD_HEADER* cmd_header = (HBA_CMD_HEADER*)clb;
  for (int i=0; i < 32; i++) {
    cmd_header[i].prdtl = 8; // 8 prdt entries per command table

    uint ct             = kalloc();
    memset(ct, 0, 256);
    cmd_header[i].ctba  = V2P(ct);
    cmd_header[i].ctbau = 0;
  }

  // 4.2.1 Received FIS Structure
  uint fb   = kalloc();
  memset(fb, 0, 256);
  port->fb  = V2P(fb);
  cprintf("fb: 0x%x\n", fb);
  port->fbu = 0;
}

// Find a free command list slot
int find_cmdslot(HBA_PORT *port)
{
	// If not set in SACT and CI, the slot is free
	uint slots = (port->sact | port->ci);
	for (int i=0; i<1; i++)
	{
		if ((slots&1) == 0)
			return i;
		slots >>= 1;
	}
	return -1;
}

// https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L560
uint ahci_identify_device(HBA_PORT* port, IDENTIFY_DEVICE_DATA* identify_data)
{
  port->is = (uint)-1; // Clear pending interrupt bits

  HBA_CMD_HEADER* cmd_header = (HBA_CMD_HEADER*)P2V(port->clb);
  cmd_header->cfl   = sizeof(FIS_REG_H2D) / /* Command FIS size */ sizeof(uint);
  cmd_header->w     = 0; // Read from device
  cmd_header->a     = 0;
  cmd_header->prdtl = 1; // PRDT entries count

  HBA_CMD_TBL* cmd_table = (HBA_CMD_TBL*)(P2V(cmd_header->ctba));

  // Setup PRDT
  cmd_table->prdt_entry[0].dba  = (uint)identify_data;
  cmd_table->prdt_entry[0].dbau = 0;
  cmd_table->prdt_entry[0].dbc  = /* 512 bytes */ sizeof(IDENTIFY_DEVICE_DATA) - 1;
  cmd_table->prdt_entry[0].i    = 1; // Interrupt on completion

  // Setup command
  FIS_REG_H2D* cfis = (FIS_REG_H2D*)(&cmd_table->cfis);

  cfis->fis_type = FIS_TYPE_REG_H2D;
  cfis->c = 1; // Command
  cfis->command = AHCI_ATA_CMD_IDENTIFY;

  cfis->device = 0; // Set device to master

  // Wait for port to be ready
  uint timeout = 0;
  while ((port->tfd & (0x80 | 0x08)) && timeout < 1000000) {
      timeout++;
  }

  if (timeout >= 1000000) {
    cprintf("ahci_identify_device() timeout\n");
    return 0; // Timeout, return failure
  }

  port->ci = 1; // Issue command

  // for (;;) {
  //   cprintf("cmd: 0x%x\n", port->cmd);
  //   cprintf("tfd: 0x%x\n", port->tfd);
  //   cprintf("ci: 0x%x\n", port->ci);
  //   cprintf("is: 0x%x\n", port->is);
  //   cprintf("serr: 0x%x\n", port->serr);
  //   cprintf("sact: 0x%x\n", port->sact);
  // }

  // Wait for completion
  while (1) {
      if ((port->ci & 1) == 0) break;
      if (port->is & (1 << 30)) // Task file error
      {
          return 0; // Read disk error
      }
  }

  return 1;
}

void satawrite(uchar* data)
{
  cprintf("satawrite() called - 0x%p\n", data);

  // https://github.com/coreboot/seabios/blob/master/src/hw/ahci.c#L34
  struct sata_cmd_fis* cfis = (struct sata_cmd_fis*)kalloc();
  memset(cfis, 0, sizeof(struct sata_cmd_fis));
  cfis->feature      = 1; /* dma */
  cfis->command      = 0xca;
  cfis->sector_count = 1;

  // int rc = ahci_command(/* port = */ 0, cfis, 1, 0, data, 10);
  return;
}

void sys_satawrite(void)
{
  uchar* data;
  argptr(1, &data, 4);
  satawrite(data);
  return;
}

// https://github.com/coreboot/seabios/blob/0026c353eb4e220af29750fcf000d48faf8d4ab3/src/hw/ahci.c#L432
void
satainit(void)
{
  // initlock(&satalock, "sata");

  // Configuration space access
  // int address = inl(0xcf8);
  // int data = inl(0xcfc);
  // cprintf("CONFIG_ADDRESS: 0x%x, CONFIG_DATA: 0x%x\n", address, data);

  printBuses();

  // https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L158
  bar5 = pci_config_readl(0, 4, 0, 0x24);
  cprintf("BAR5: 0x%x\n", bar5);

  // Enable membar and busmastering
  // Enable interrupts, DMA, and memory space access in the PCI command register
  // https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L159
  ushort pci_cmd = pci_config_readw(0, 4, 0, 0x04);
  pci_cmd = pci_cmd | (1 << 1) | (1 << 2) | (1 << 8) | (1 << 10);
  pci_config_writel(0, 4, 0, 0x04, pci_cmd);

  // Request HBA reset
  // https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L172
  bar5->ghc |= HBA_RESET;
  // Wait for reset to complete
  while (bar5->ghc & HBA_RESET);
  cprintf("GHC: 0x%x\n", bar5->ghc);

  // Set the AHCI Enable bit
  // https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L183
  bar5->ghc |= (AHCI_ENABLE);
  cprintf("GHC: 0x%x\n", bar5->ghc);

  cprintf("cap: 0x%x\n", bar5->cap);
  cprintf("Port Implemented: 0x%x\n", bar5->pi);

  // for ports
  for (uint pnr=0; pnr <= 0x1f; pnr++) {
    // Not implemented
    if (!(bar5->pi & (1 << pnr)))
      continue;

    cprintf("Checking port %d...\n", pnr);

    HBA_PORT* port = &bar5->ports[pnr];
    cprintf("port: 0x%x\n", port);
    cprintf("Port signature: 0x%x\n", port->sig);

    // Alloc CLB and FB
    ahci_port_alloc(port);

    stop_cmd(port);

    start_cmd(port);

    for (uint i=0; i<1000000; i++) {
      if ((port->ssts & 0x07) == 0x03) {
        goto linkup;
      }
    }
    cprintf("AHCI/%d: link down\n", pnr);
    continue;

linkup:
    cprintf("AHCI/%d: link up\n", pnr);
    cprintf("Port signature: 0x%x\n", port->sig);

    for (int i=0; i<1000000; i++) {
      if (!(port->tfd & (0x80 | 0x08))) {
        goto identdev;
      }
    }
    continue;

identdev:

    /* start device */
    // Set ST (bit0)
	  port->cmd |= HBA_PxCMD_ST;

    // Identify device
    // https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L542
    IDENTIFY_DEVICE_DATA* identify_data = (IDENTIFY_DEVICE_DATA*)kalloc();
    memset((uchar*)identify_data, 0, 512);
    if (!ahci_identify_device(port, identify_data)) {
      cprintf("ahci_identify_device() failed\n");
    }

    cprintf("%d\n", identify_data->data[2]);
    for (int i=0; i < 512; i++) {
      cprintf("%d", identify_data->data[i]);
    }
    cprintf("\n");
  }

  return;
}