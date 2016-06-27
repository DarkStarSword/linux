/*
 * Copyright 2015 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
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
 * in cxl mode.
 */
int pnv_cxl_enable_phb_kernel2_api(struct pci_dev *dev, bool enable, int quirks)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct module *cxl_module;

	if (!enable) {
		/*
		 * Once cxl mode is enabled on the PHB, there is currently no
		 * known safe method to disable it again, and trying risks a
		 * checkstop. If we can find a way to safely disable cxl mode
		 * in the future we can revisit this, but for now the only sane
		 * thing to do is to refuse to disable cxl mode:
		 */
		return -EPERM;
	}

	/*
	 * Hold a reference to the cxl module since several PHB operations now
	 * depend on it, and it would be insane to allow it to be removed so
	 * long as we are in this mode (and since we can't safely disable this
	 * mode once enabled...).
	 */
	mutex_lock(&module_mutex);
	cxl_module = find_module("cxl");
	if (cxl_module)
		__module_get(cxl_module);
	mutex_unlock(&module_mutex);
	if (!cxl_module)
		return -ENODEV;

	phb->flags |= PNV_PHB_FLAG_CXL;
	hose->controller_ops = pnv_cxl_cx4_ioda_controller_ops;

	return 0;
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
 *
 * Since this procedure is tightly coupled to the cxl module, the bulk of the
 * code is there to minimise the number of symbols that need to be built into
 * the kernel.
 */
int pnv_cxl_cx4_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;

	if (WARN_ON(!phb) || !phb->msi_bmp.bitmap)
		return -ENODEV;

	return cxl_cx4_setup_msi_irqs(pdev, nvec, type);
}

void pnv_cxl_cx4_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	struct pnv_phb *phb = hose->private_data;

	if (WARN_ON(!phb))
		return;

	cxl_cx4_teardown_msi_irqs(pdev);
}
