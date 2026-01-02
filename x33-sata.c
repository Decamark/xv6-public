// https://wiki.osdev.org/AHCI

#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "x86.h"
#include "memlayout.h"
#include "x33-sata.h"

// // submit ahci command + wait for result
// // https://wiki.osdev.org/AHCI
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

volatile HBA_MEM* bar5;

// Start command engine
void start_cmd(HBA_PORT *port)
{
  	while (port->cmd & HBA_PxCMD_CR)
		;

  cprintf("tfd: 0x%x\n", port->tfd);
  // Enable FIS recv
	// Set FRE (bit4) and ST (bit0)
	port->cmd |= HBA_PxCMD_FRE;
  cprintf("tfd: 0x%x\n", port->tfd);

  // Enable interrupts
  // port->ie = 0b00000000000000000000000000000001;
  port->ie = 0b11111111111111111111111111111110;
  // NOTICE: an interrupt occurs here

  // spin up
  // https://github.com/coreboot/seabios/blob/master/src/hw/ahci.c#L447
  port->cmd &= ~HBA_PxCMD_ICC_MASK;
  port->cmd |= HBA_PxCMD_SPIN_UP | HBA_PxCMD_POWER_ON | HBA_PxCMD_ICC_ACTIVE;

  for (uint timeout=0; timeout < 1000000; timeout++) {
    if (!(port->tfd & (0x80 | 0x08))) {
      goto startdev;
    }
  }
  return;

startdev:
	port->cmd |= HBA_PxCMD_ST;

  cprintf("port->cmd: 0x%x\n", port->cmd);
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

  // // Disable interrupt
  port->ie = 0;

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
  for (int i=0; i < 1; i++) {
    cmd_header[i].prdtl = 8; // 8 prdt entries per command table

    uint cmd_table      = kalloc();
    memset(cmd_table, 0, 256);
    cmd_header[i].ctba  = V2P(cmd_table);
    cmd_header[i].ctbau = 0;
    cprintf("ctba: 0x%x\n", cmd_table);
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
  cmd_header->cfl   = sizeof(FIS_REG_H2D); /* Command FIS size */
  cmd_header->w     = 0; // Read from device
  cmd_header->a     = 0;
  cmd_header->prdtl = 1; // PRDT entries count

  HBA_CMD_TBL* cmd_table = (HBA_CMD_TBL*)(P2V(cmd_header->ctba));

  // Setup command
  FIS_REG_H2D* cfis = (FIS_REG_H2D*)(&cmd_table->cfis);
  cfis->fis_type = FIS_TYPE_REG_H2D;
  cfis->c = 1; // Command
  cfis->command = AHCI_ATA_CMD_IDENTIFY;
  cfis->device = 0; // Set device to master
  cfis->pmport = 1 << 7;

  // Setup PRDT
  cmd_table->prdt_entry[0].dba  = identify_data;
  cmd_table->prdt_entry[0].dbau = 0;
  cmd_table->prdt_entry[0].dbc  = /* 512 bytes */ sizeof(IDENTIFY_DEVICE_DATA) - 1;
  cmd_table->prdt_entry[0].i    = 1; // Interrupt on completion

  // Wait for port to be ready
  uint timeout = 0;
  while ((port->tfd & (0x80 | 0x08)) && timeout < 1000000) {
      timeout++;
  }

  if (timeout >= 1000000) {
    cprintf("[ahci_identify_device] timeout\n");
    return 0; // Timeout, return failure
  }

  port->ci = 1; // Issue command

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

int ahci_command(HBA_PORT* port, uchar w, uchar fis_type, uchar command, uchar* dba, uint dbc)
{
  // Clear pending interrupt bits
  port->is = (uint)-1;

  HBA_CMD_HEADER* cmd_header = (HBA_CMD_HEADER*)P2V(port->clb);
  cmd_header->cfl   = sizeof(FIS_REG_H2D) / sizeof(uint); /* Command FIS size */
  cmd_header->w     = w; /* read/write */
  // cmd_header->a     = 0;
  cmd_header->prdtl = 5;  /* PRDT entries count */

  HBA_CMD_TBL* cmd_table = (HBA_CMD_TBL*)P2V(cmd_header->ctba);
  memset(cmd_table, 0, sizeof(HBA_CMD_TBL));
  cprintf("[ahci_command] cmd_table: 0x%x\n", cmd_table);

  // Setup Command FIS
  FIS_REG_H2D* cfis = (FIS_REG_H2D*)(&cmd_table->cfis);
  cprintf("[ahci_command] cfis: 0x%x\n", cfis);
  cfis->fis_type    = fis_type;
  cfis->c           = 1; /* command */
  cfis->command     = command;
  cfis->device      = 1 << 6;
  // IMPORTANT: This needs to be set properly
  cfis->lba0 = 0xff;
  cfis->lba1 = 0xff;
  cfis->lba2 = 0xff;
  cfis->lba3 = 0xff;
  cfis->lba4 = 0xff;
  cfis->lba5 = 0xff;
  // IMPORTANT: This needs to be set properly
  cfis->countl = 1;

  // Setup PRDT
  cmd_table->prdt_entry[0].dba  = 0x41414141;
  cmd_table->prdt_entry[0].dbau = 0;
  cmd_table->prdt_entry[0].dbc  = 4096 - 1; /* byte count */
  cmd_table->prdt_entry[0].i    = 1;    /* interrupt on completion */

  // Wait for port to be ready
  // uint timeout = 0;
  // while ((port->tfd & (0x80 | 0x08))) {
  //   if (timeout >= 1000000) {
  //     cprintf("[ahci_command] timeout\n");
  //     return 0; // Timeout, return failure
  //   }

  //   timeout++;
  // }

  // bar5->is = bar5->is;
  // port->is   = 0xffffffff;
  // port->serr = 0xffffffff;

  // Issue command
  port->ci = 1;

  // while (1) {
  //   cprintf("port->ci: 0x%x\n", port->ci);
  // }

  // Wait for completion
  while (1)
  {
    // if (port->is & 1) {
    //   cprintf("[ahci_command] is: %d\n", port->is);
    //   char* fb = P2V(port->fb);
    //   char status = *(fb + 0x1c + 0x04 + 0x14 + 0x0c + 2);
    //   char error  = *(fb + 0x1c + 0x04 + 0x14 + 0x0c + 3);
    //   cprintf("[ahci_command] status: %d, error: %d\n", status, error);
    //   cprintf("[ahci_command] serr: %d\n", port->serr);
    //   break;
    // }
    // if (port->is & (1 << 30)) {
    //   cprintf("Error: Task file error\n");
    //   return 0;
    // }
    if ((port->ci & 1) == 0) {
      cprintf("[ahci_command] is: %d\n", port->is);
      char* fb = P2V(port->fb);
      char type = *(fb + 0x40);
      char status = *(fb + 0x40 + 2);
      char error  = *(fb + 0x40 + 3);
      cprintf("[ahci_command] type: 0x%x, status: %d, error: %d\n", type, status, error);
      cprintf("[ahci_command] tfd: %d\n", port->tfd);
      break;
    }
  }

  return 1;
}

void satawrite(uchar* data, uint size)
{
  cprintf("[satawrite] Trying to write 0x%p\n", data);

  HBA_PORT* port = &bar5->ports[0]; // Use port 0
  uchar* testdata = kalloc();
  *(uint*)testdata = 0x41424344;
  int rc = ahci_command(port, 1, FIS_TYPE_REG_H2D, 0xc8, testdata, 511);
  if (!rc) {
    cprintf("[satawrite] ahci_command() failed\n");
  }
  return;
}

void sys_satawrite(void)
{
  uchar* data;
  argptr(1, &data, 4);
  satawrite(data, 10);
  return;
}

void
satainit(void)
{
  printBuses();

  // Enable membar and busmastering
  // DISABLE interrupts, DMA, and memory space access in the PCI command register
  // We disable interrupts to enable INTx
  // https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L159
  ushort pci_cmd = pci_config_readw(0, 4, 0x20, 0x04);
  pci_cmd = pci_cmd | (1 << 1) | (1 << 2) | (1 << 8) | ~(1 << 10);
  pci_config_writel(0, 4, 0, 0x04, pci_cmd);

  uchar irq = pci_config_readb(0, 4, 0x20, 0x3c);
  cprintf("[satainit] IRQ: 0x%x\n", irq);
  ioapicenable((int)irq, 0);

  // https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L158
  bar5 = pci_config_readl(0, 4, 0x20, 0x24);
  cprintf("[satainit] BAR5: 0x%x\n", bar5);

  // Request HBA reset
  // https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L172
  bar5->ghc |= HBA_RESET;
  // Wait for reset to complete
  while (bar5->ghc & HBA_RESET);

  // Set the AHCI Enable bit
  // https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L183
  bar5->ghc |= (AHCI_ENABLE | AHCI_IE);

  cprintf("bar5->pi: 0x%x\n", bar5->pi);
  cprintf("bar5->cap: 0x%x\n", bar5->cap);

  // for ports
  for (uint pnr=0; pnr <= 0x1f; pnr++) {
    // Not implemented
    if (!(bar5->pi & (1 << pnr)))
      continue;

    cprintf("[satainit] Checking port %d...\n", pnr);

    volatile HBA_PORT* port = &bar5->ports[pnr];

    stop_cmd(port);

    // Alloc CLB and FB
    ahci_port_alloc(port);

    start_cmd(port);

    for (uint i=0; i<100000; i++) {
      if ((port->ssts & 0b1111) == 0x03) {
            cprintf("[satainit] Status: %d\n", port->ssts);
        goto linkup;
      }
    }
    cprintf("[satainit] AHCI/%d: link down\n", pnr);
    continue;

linkup:
    cprintf("[satainit] AHCI/%d: link up\n", pnr);
    cprintf("[satainit] sig: %x\n", port->sig);

    for (int i=0; i<1000000; i++) {
      if (!(port->tfd & (0x80 | 0x08))) {
        goto identdev;
      }
    }
    continue;

identdev:

    // Identify device
    // https://github.com/KingVentrix007/AthenX-3.0/blob/master/drivers/disk/ahci/ahci_main.c#L542
    IDENTIFY_DEVICE_DATA* identify_data = (IDENTIFY_DEVICE_DATA*)kalloc();
    memset((uchar*)identify_data, 0, 512);
    // for (int i=0; i < 512; i++) {
    //   cprintf("%d", identify_data->data[i]);
    // }
    // cprintf("\n");
    // if (!ahci_identify_device(port, identify_data)) {
    //   cprintf("[satainit] ahci_identify_device() failed\n");
    // }
    // for (int i=0; i < 512; i++) {
    //   cprintf("%d", identify_data->data[i]);
    // }
    // cprintf("\n");
  }

  return;
}