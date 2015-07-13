/*
 * Copyright 2015 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

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
