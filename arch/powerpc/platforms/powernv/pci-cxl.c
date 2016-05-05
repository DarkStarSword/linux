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
		if (quirks & CXL_QUIRK_CX4)
			phb->flags |= PNV_PHB_FLAG_CXL_QUIRK_CX4;
	} else {
		phb->flags &= ~(PNV_PHB_FLAG_CXL | PNV_PHB_FLAG_CXL_QUIRK_CX4);
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
 * This is a special version of pnv_setup_msi_irqs specifially for the Mellanox
 * CX4 card while in cxl mode.
 *
 * When the card is in normal PCI mode it will use an MSI-X table as usual, but
 * while in cxl mode the card routes interrupts through the cxl XSL
 * (Translation Service Layer) instead, which still sends out MSI-X interrupts
 * on the bus, but uses the cxl mechanism to determine the address and data to
 * use for the interrupt instead of the MSI-X table.
 *
 * For this particular card in this mode, the MSI-X table is still used, but
 * instead of defining the address and data to use on the PCI bus, it defines
 * the cxl PE ID (Process Element ID, not to be confused with Partitionable
 * Endpoint) and AFU interrupt number to send to the XSL. The XSL will then
 * look up the IVTE offset and ranges for the corresponding PE entry to map
 * that back to an address to use for the MSI packet (data is always 0 for cxl
 * interrupts) and therefore the hwirq number.
 *
 * This function handles setting up the IVTE entries for the XSL to use and the
 * MSI-X table for the CX4 to use.
 */
int pnv_cxl_cx4_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct msi_desc *entry;
	struct msi_msg msg;
	struct cxl_context *ctx;
	int hwirq;
	unsigned int virq;
	int rc;
	int afu_irq = 1;

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
	rc = cxl_allocate_afu_irqs(ctx, nvec);
	if (rc) {
		pr_warn("%s: Failed to find enough free MSIs\n", pci_name(pdev));
		return rc;
	}

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

		if (phb->flags & PNV_PHB_FLAG_CXL_QUIRK_CX4) {
			/*
			 * This is a quirk specific to the CX4 card while in
			 * cxl mode, which uses the MSI-X table to route
			 * interrupts to the XSL instead of over the PCI bus.
			 */
			msg.address_hi = 0;
			msg.address_lo = cxl_process_element(ctx) << 16 | afu_irq;
			msg.data = 0;
			dev_info(&pdev->dev, "MSIX[%i] PE=%i LISN=%i msg.address_lo=%08x\n",
					afu_irq, cxl_process_element(ctx), afu_irq, msg.address_lo);
		}

		irq_set_msi_desc(virq, entry);
		pci_write_msi_msg(virq, &msg);

		afu_irq++;
	}
	WARN_ON(afu_irq != nvec + 1);
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
