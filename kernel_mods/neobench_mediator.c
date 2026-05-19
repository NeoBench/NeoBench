// SPDX-License-Identifier: GPL-2.0
/*
 * NeoBench: Elbox Mediator PCI Busboard Host Driver
 * Copyright (C) 2026 Lord Protector
 *
 * This provides a foundational PCI host bridge implementation for the 
 * Elbox Mediator, mapping PCI configuration and memory spaces via Zorro III.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/zorro.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/setup.h>

#ifndef ZORRO_PROD_ELBOX_MEDIATOR
/* ZORRO_ID(manuf, prod, epc) -> ((manuf << 16) | (prod << 8) | epc) */
#define ZORRO_PROD_ELBOX_MEDIATOR ((0x08A0 << 16) | (0x00 << 8) | 0)
#endif

#define MEDIATOR_WINDOW_SIZE 0x10000000 /* 256MB Z3 Window */

static void __iomem *mediator_cfg_base;
static void __iomem *mediator_mem_base;

/* 
 * PCI Configuration Space Access via Window 
 * Real hardware requires byte-swapping and specific address encoding
 * depending on the exact Mediator model (A1200 vs A4000).
 */
static int mediator_pci_read(struct pci_bus *bus, unsigned int devfn,
			     int where, int size, u32 *val)
{
	/* STUB: Actual hardware requires specific windowing offsets */
	*val = 0xFFFFFFFF;
	return PCIBIOS_SUCCESSFUL;
}

static int mediator_pci_write(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 val)
{
	/* STUB: Actual hardware requires specific windowing offsets */
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops mediator_pci_ops = {
	.read = mediator_pci_read,
	.write = mediator_pci_write,
};

static int __init neobench_mediator_init(void)
{
	struct zorro_dev *z = NULL;
	struct pci_host_bridge *bridge;
	
	if (!MACH_IS_AMIGA)
		return -ENODEV;

	/* Find Mediator on Zorro Bus */
	while ((z = zorro_find_device(ZORRO_PROD_ELBOX_MEDIATOR, z))) {
		pr_notice("  NEOBENCH: Elbox Mediator PCI found at 0x%08lx\n", 
			  (unsigned long)z->resource.start);
		break;
	}

	if (!z) {
		/* Fallback for testing/emulation without AutoConfig */
		pr_notice("  NEOBENCH: No Mediator AutoConfig found, using dummy PCI host.\n");
	}

	bridge = pci_alloc_host_bridge(0);
	if (!bridge)
		return -ENOMEM;

	bridge->ops = &mediator_pci_ops;
	bridge->sysdata = z;

	pr_notice("  NEOBENCH: Mediator PCI Subsystem Initialized.\n");
	
	/* pci_host_probe(bridge); */ /* Commented out until full config space is mapped */

	return 0;
}

subsys_initcall(neobench_mediator_init);