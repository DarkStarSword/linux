/*
 * Copyright 2015 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/msi.h>
#include <asm/pci-bridge.h>
#include <asm/pnv-pci.h>
#include <misc/cxl.h>

#include "pci.h"

/*
 * Sets flags and switches the controller ops to enable the cxl kernel2 api.
 * The original cxl kernel API operated on a virtual PHB, whereas the kernel2
 * api operates on a real PHB (but otherwise shares much of the same
 * implementation), and is currently restricted to the Mellanox CX-4 card when
 * in cxl mode. The CX4 card has some additional quirks that we need to handle.
 */
void pnv_cxl_enable_phb_kernel2_api(struct pci_dev *dev, bool enable, int quirks)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;

	if (enable) {
		phb->flags |= PNV_PHB_FLAG_CXL;
		hose->controller_ops = pnv_cxl_cx4_ioda_controller_ops;
	} else {
		phb->flags &= ~PNV_PHB_FLAG_CXL;
		hose->controller_ops = pnv_pci_ioda_controller_ops;
	}
}
EXPORT_SYMBOL(pnv_cxl_enable_phb_kernel2_api);

bool pnv_pci_on_cxl_phb(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;

	return !!(phb->flags & PNV_PHB_FLAG_CXL);
}
EXPORT_SYMBOL(pnv_pci_on_cxl_phb);

struct cxl_afu *pnv_cxl_phb_to_afu(struct pci_controller *hose)
{
	struct pnv_phb *phb = hose->private_data;

	return (struct cxl_afu *)phb->cxl_afu;
}
EXPORT_SYMBOL_GPL(pnv_cxl_phb_to_afu);

void pnv_cxl_phb_set_peer_afu(struct pci_dev *dev, struct cxl_afu *afu)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;

	phb->cxl_afu = afu;
}
EXPORT_SYMBOL_GPL(pnv_cxl_phb_set_peer_afu);

bool pnv_cxl_enable_device_hook(struct pci_dev *dev, struct pnv_phb *phb)
{
	struct cxl_afu *afu = phb->cxl_afu;

	/* No special handling for cxl function: */
	if (PCI_FUNC(dev->devfn) == 0)
		return true;

	if (!afu) {
		dev_WARN(&dev->dev, "Attempted to enable function > 0 on CXL PHB without a peer AFU\n");
		return false;
	}

	dev_info(&dev->dev, "Enabling function on CXL enabled PHB with peer AFU\n");

	return cxl_pci_associate_default_context(dev, afu, PCI_FUNC(dev->devfn));
}

/*
 * This is a special version of pnv_setup_msi_irqs for cards in cxl mode. This
 * function handles setting up the IVTE entries for the XSL to use.
 *
 * We are currently not filling out the MSIX table, since the only currently
 * supported adapter (CX4) uses a custom MSIX table format in cxl mode and it
 * is up to their driver to fill that out.
 *
 * In the future we may fill out the MSIX table (and change the IVTE entries to
 * be an index to the MSIX table) for adapters implementing the Full MSI-X mode
 * described in the CAIA.
 */
int pnv_cxl_cx4_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct msi_desc *entry;
	struct cxl_context *ctx;
	int remaining;
	unsigned int virq;
	int afu_irq = 1;
	int hwirq;
	int rc;

	if (WARN_ON(!phb) || !phb->msi_bmp.bitmap)
		return -ENODEV;

	if (pdev->no_64bit_msi && !phb->msi32_support)
		return -ENODEV;

	/*
	 * FIXME: Gate this on getting the cxl module, probably just do this
	 * once when switching the phb to cxl mode.
	 */
	ctx = cxl_get_context(pdev);
	if (WARN_ON(!ctx))
		return -ENODEV;

	/*
	 * cxl has to fit all the interrupts in up to four ranges (one of which
	 * is used by a multiplexed DSI interrupt), so to maximise the chance
	 * of success we call into the cxl driver to allocate them from the
	 * bitmap and set up the ranges:
	 *
	 * FIXME: This differs a little from the regular MSI-X case in the
	 * event of a failure - if the bitmap allocation fails at all we fail
	 * the whole call, and if the bitmap succeeds but something else fails
	 * we leave some interrupts allocated in the bitmap, which will be
	 * freed in the teardown function, but might be nice to release them
	 * here. Given that this routine is used by exactly one driver and the
	 * PHB has to be dedicated to the card in cxl mode, let's see if it's a
	 * problem in practice before trying to do anything heroic.
	 */
	remaining = nvec;
	while (remaining > 0) {
		rc = cxl_allocate_afu_irqs(ctx, min(remaining, ctx->afu->max_irqs));
		if (rc) {
			pr_warn("%s: Failed to find enough free MSIs\n", pci_name(pdev));
			return rc;
		}
		remaining -= ctx->afu->max_irqs;

		if (remaining > 0) {
			new_ctx = cxl_dev_context_init(pdev);
			if (!new_ctx) {
				// FIXME
			}
			list_add(new_ctx->list, ctx->list);
			ctx = new_ctx;
		}
	}

	ctx = cxl_get_context(pdev);
	for_each_pci_msi_entry(entry, pdev) {
		if (!entry->msi_attrib.is_64 && !phb->msi32_support) {
			pr_warn("%s: Supports only 64-bit MSIs\n",
				pci_name(pdev));
			return -ENXIO;
		}

		hwirq = cxl_afu_irq_to_hwirq(ctx, afu_irq);
		virq = irq_create_mapping(NULL, hwirq);
		if (virq == NO_IRQ) {
			pr_warn("%s: Failed to map cxl mode CX4 MSI to linux irq\n",
				pci_name(pdev));
			return -ENOMEM;
		}

		rc = pnv_cxl_ioda_msi_setup(pdev, hwirq, virq);
		if (rc) {
			pr_warn("%s: Failed to setup cxl mode CX4 MSI\n", pci_name(pdev));
			irq_dispose_mapping(virq);
			return rc;
		}

		irq_set_msi_desc(virq, entry);

		afu_irq++;
		if (afu_irq > ctx->afu->max_irqs) {
			// TODO: Use symbol_request / symbol_get to clean up getting symbols
			ctx = cxl_next_context(ctx);
			afu_irq = 1;
		}
	}
	return 0;
}

void pnv_cxl_cx4_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct msi_desc *entry;
	irq_hw_number_t hwirq;
	struct cxl_context *ctx;

	if (WARN_ON(!phb))
		return;

	ctx = cxl_get_context(pdev);
	if (WARN_ON(!ctx))
		return;

	for_each_pci_msi_entry(entry, pdev) {
		if (entry->irq == NO_IRQ)
			continue;
		hwirq = virq_to_hw(entry->irq);
		irq_set_msi_desc(entry->irq, NULL);
		irq_dispose_mapping(entry->irq);
	}

	cxl_free_afu_irqs(ctx);
}
