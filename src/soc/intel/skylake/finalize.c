/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2014 Google Inc.
 * Copyright (C) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <arch/io.h>
#include <bootstate.h>
#include <chip.h>
#include <console/console.h>
#include <console/post_codes.h>
#include <cpu/x86/smm.h>
#include <reg_script.h>
#include <spi-generic.h>
#include <stdlib.h>
#include <soc/lpc.h>
#include <soc/pci_devs.h>
#include <soc/pcr.h>
#include <soc/pm.h>
#include <soc/pmc.h>
#include <soc/spi.h>
#include <soc/systemagent.h>
#include <device/pci.h>
#include <chip.h>

#define PCH_P2SB_EPMASK0		0xB0
#define PCH_P2SB_EPMASK(mask_number) 	PCH_P2SB_EPMASK0 + (mask_number * 4)

#define PCH_P2SB_E0			0xE0

static void pch_configure_endpoints(device_t dev, int epmask_id, uint32_t mask)
{
	uint32_t reg32;

	reg32 = pci_read_config32(dev, PCH_P2SB_EPMASK(epmask_id));
	pci_write_config32(dev, PCH_P2SB_EPMASK(epmask_id), reg32 | mask);
}

static void pch_disable_heci(void)
{
	device_t dev;
	u8 reg8;
	uint32_t mask;

	dev = PCH_DEV_P2SB;

	/*
	 * if p2sb device 1f.1 is not present or hidden in devicetree
	 * p2sb device becomes NULL
	 */
	if (!dev)
		return;

	/* unhide p2sb device */
	pci_write_config8(dev, PCH_P2SB_E0 + 1, 0);

	/* disable heci */
	pcr_andthenor32(PID_PSF1, PSF_BASE_ADDRESS + PCH_PCR_PSFX_T0_SHDW_PCIEN,
				~0, PCH_PCR_PSFX_T0_SHDW_PCIEN_FUNDIS);

	/* Remove the host accessing right to PSF register range. */
	/* Set p2sb PCI offset EPMASK5 C4h [29, 28, 27, 26] to [1, 1, 1, 1] */
	mask = (1 << 29) | (1 << 28) | (1 << 27)  | (1 << 26);
	pch_configure_endpoints(dev, 5, mask);

	/* Set the "Endpoint Mask Lock!", P2SB PCI offset E2h bit[1] to 1. */
	reg8 = pci_read_config8(dev, PCH_P2SB_E0 + 2);
	pci_write_config8(dev, PCH_P2SB_E0 + 2, reg8 | (1 << 1));

	/* hide p2sb device */
	pci_write_config8(dev, PCH_P2SB_E0 + 1, 1);
}

static void pch_finalize_script(void)
{
	device_t dev;
	uint32_t reg32, hsfs;
	void *spibar = get_spi_bar();
	u16 tcobase;
	u16 tcocnt;
	uint8_t *pmcbase;
	config_t *config;
	u32 pmsyncreg;

	/* Set SPI opcode menu */
	write16(spibar + SPIBAR_PREOP, SPI_OPPREFIX);
	write16(spibar + SPIBAR_OPTYPE, SPI_OPTYPE);
	write32(spibar + SPIBAR_OPMENU_LOWER, SPI_OPMENU_LOWER);
	write32(spibar + SPIBAR_OPMENU_UPPER, SPI_OPMENU_UPPER);
	/* Lock SPIBAR */
	hsfs = read32(spibar + SPIBAR_HSFS);
	hsfs |= SPIBAR_HSFS_FLOCKDN;
	write32(spibar + SPIBAR_HSFS, hsfs);

	/*TCO Lock down */
	tcobase = pmc_tco_regs();
	tcocnt = inw(tcobase + TCO1_CNT);
	tcocnt |= TCO_LOCK;
	outw(tcocnt, tcobase + TCO1_CNT);

	/* Lock down ABASE and sleep stretching policy */
	dev = PCH_DEV_PMC;
	reg32 = pci_read_config32(dev, GEN_PMCON_B);
	reg32 |= (SLP_STR_POL_LOCK | ACPI_BASE_LOCK);
	pci_write_config32(dev, GEN_PMCON_B, reg32);

	/* PMSYNC */
	pmcbase = pmc_mmio_regs();
	pmsyncreg = read32(pmcbase + PMSYNC_TPR_CFG);
	pmsyncreg |= PMSYNC_LOCK;
	write32(pmcbase + PMSYNC_TPR_CFG, pmsyncreg);

	/* we should disable Heci1 based on the devicetree policy */
	config = dev->chip_info;
	if (config->HeciEnabled == 0)
		pch_disable_heci();
}

static void soc_lockdown(void)
{
	u8 reg8;
	device_t dev;
	const struct device *dev1 = dev_find_slot(0, PCH_DEVFN_LPC);
	const struct soc_intel_skylake_config *config = dev1->chip_info;

	/* Global SMI Lock */
	if (config->LockDownConfigGlobalSmi == 0) {
		dev = PCH_DEV_PMC;
		reg8 = pci_read_config8(dev, GEN_PMCON_A);
		reg8 |= SMI_LOCK;
		pci_write_config8(dev, GEN_PMCON_A, reg8);
	}

	/* Bios Interface Lock */
	if (config->LockDownConfigBiosInterface == 0) {
		pci_write_config8(PCH_DEV_LPC, BIOS_CNTL,
				  pci_read_config8(PCH_DEV_LPC,
						   BIOS_CNTL) | LPC_BC_BILD);
		/* Reads back for posted write to take effect */
		pci_read_config8(PCH_DEV_LPC, BIOS_CNTL);
		pci_write_config32(PCH_DEV_SPI, SPIBAR_BIOS_CNTL,
				   pci_read_config32(PCH_DEV_SPI,
						     SPIBAR_BIOS_CNTL) |
				   SPIBAR_BC_BILD);
		/* Reads back for posted write to take effect */
		pci_read_config32(PCH_DEV_SPI, SPIBAR_BIOS_CNTL);
		/* GCS reg of DMI */
		pcr_andthenor8(PID_DMI, R_PCH_PCR_DMI_GCS, 0xFF,
			       B_PCH_PCR_DMI_GCS_BILD);
	}

	/* Bios Lock */
	if (config->LockDownConfigBiosLock == 0) {
		pci_write_config8(PCH_DEV_LPC, BIOS_CNTL,
				  pci_read_config8(PCH_DEV_LPC,
						   BIOS_CNTL) | LPC_BC_LE);
		pci_write_config8(PCH_DEV_SPI, BIOS_CNTL,
				  pci_read_config8(PCH_DEV_SPI,
						   BIOS_CNTL) | SPIBAR_BC_LE);
	}

	/* SPIEiss */
	if (config->LockDownConfigSpiEiss == 0) {
		pci_write_config8(PCH_DEV_LPC, BIOS_CNTL,
				  pci_read_config8(PCH_DEV_LPC,
						   BIOS_CNTL) | LPC_BC_EISS);
		pci_write_config8(PCH_DEV_SPI, BIOS_CNTL,
				  pci_read_config8(PCH_DEV_SPI,
						   SPIBAR_BIOS_CNTL) |
				  SPIBAR_BC_EISS);
	}
}

static void soc_finalize(void *unused)
{
	printk(BIOS_DEBUG, "Finalizing chipset.\n");

	pch_finalize_script();

	soc_lockdown();

	/* Indicate finalize step with post code */
	post_code(POST_OS_BOOT);
}

BOOT_STATE_INIT_ENTRY(BS_OS_RESUME, BS_ON_ENTRY, soc_finalize, NULL);
BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_LOAD, BS_ON_EXIT, soc_finalize, NULL);
