#ifdef CONFIG_MLX5_CAPI

#include <linux/mlx5/driver.h>
#include <misc/cxl.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include "mlx5_core.h"

#define CXL_PCI_VSEC_ID  0x1280
#define CXL_READ_VSEC_MODE_CONTROL(dev, vsec, dest) \
		pci_read_config_byte(dev, vsec + 0xa, dest)
#define CXL_VSEC_PROTOCOL_ENABLE 0x01

static int find_cxl_vsec(struct pci_dev *dev)
{
	int vsec = 0;
        u16 val;

        while ((vsec = pci_find_next_ext_capability(dev, vsec, PCI_EXT_CAP_ID_VNDR))) {
                pci_read_config_word(dev, vsec + 0x4, &val);
                if (val == CXL_PCI_VSEC_ID)
                        return vsec;
        }
        return 0;

}

static int mlx5_capi_get_capi_mode (struct pci_dev *dev, int vsec, bool *mode)
{
	u8 val;
	int rc;

	if ((rc = CXL_READ_VSEC_MODE_CONTROL(dev, vsec, &val)))
		return rc;	

	if (val & CXL_VSEC_PROTOCOL_ENABLE)
		*mode = 1;
	else
		*mode = 0;

	return 0;
}

static struct pci_dev *
mlx5_capi_find_function0_dev(struct pci_dev *pdev)
{
	struct pci_dev *dev = NULL;
	unsigned int devfn = pdev->devfn & 0xF8;

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
        	if ((dev->bus == pdev->bus) &&
		    (dev->devfn == devfn))
                	return dev;
	}
	return NULL;
}

/*
 * In cxl mode, the CX4 uses a custom MSIX table format with the PE and LISN to
 * route interrupts to the XSL instead of over the PCI bus.
 */
void mlx5_configure_msix_table_capi(struct pci_dev *pdev)
{
	struct cxl_context *ctx;
	struct msi_desc *entry;
	unsigned int virq;
	int hwirq;
	int afu_irq = 1;
	struct msi_msg msg;

	ctx = cxl_get_context(pdev);
	if (WARN_ON(!ctx))
		return;

	for_each_pci_msi_entry(entry, pdev) {
		hwirq = cxl_afu_irq_to_hwirq(ctx, afu_irq);
		virq = irq_find_mapping(NULL, hwirq);

		/*
		 * Using an unconditional swab32 here as
		 * pci_write_msi_msg will write these values as
		 * little-endian, but the card expects these to be in
		 * big-endian.
		 */
		msg.address_hi = 0;
		msg.address_lo = 0;
		msg.data = swab32((afu_irq << 28) | cxl_process_element(ctx));

		pci_write_msi_msg(virq, &msg);

		afu_irq++;
		if (afu_irq > cxl_get_max_irqs_per_process(pdev)) {
			ctx = cxl_next_context(ctx);
			afu_irq = 1;
		}
	}
}

int mlx5_capi_cleanup(struct mlx5_core_dev *dev,
		      struct pci_dev *pdev)
{
	struct mlx5_priv      *priv;
	struct mlx5_capi_priv *capi;
	struct cxl_context    *capi_context;

	priv = &dev->priv;
	capi = &priv->capi;

	if (!capi->cxl_mode)
		return 0;

	capi_context = cxl_get_context(pdev);
	if (!capi_context) {
		mlx5_core_err(dev, "No default PE context\n");
		return -ENODEV;
	}

	cxl_stop_context(capi_context);
	cxl_stop_context(capi->direct_ctx);		
	cxl_release_context(capi->direct_ctx);
	return 0;
}

/* This function is called by both normal flow and resume flow */
int mlx5_capi_setup(struct mlx5_core_dev *dev, struct pci_dev *pdev)
{
	struct mlx5_priv      *priv;
	struct mlx5_capi_priv *capi;
	struct cxl_context    *capi_context;
	int err = 0;

	priv = &dev->priv;
	capi = &priv->capi;

	if (!capi->cxl_mode) 
		return 0;
	
	/* Non translated PE */
	capi_context = cxl_dev_context_init(pdev);
	if (!capi_context) {
		mlx5_core_err(dev, "Cannot allocate PE context!\n");
		err = -ENODEV;
		goto out;
	}
	capi->direct_pe = cxl_process_element(capi_context);
	cxl_set_translation_mode(capi_context, true);
	cxl_start_context(capi_context, 0, NULL);
	capi->direct_ctx = capi_context;

	/* Default PE */
	capi_context = cxl_get_context(pdev);
	if (!capi_context) {
		mlx5_core_err(dev, "No default PE context!\n");
		cxl_stop_context(capi->direct_ctx);
		err = -ENODEV;
		goto out;
	}
	capi->default_pe = cxl_process_element(capi_context);
	cxl_start_context(capi_context, 0, NULL);

	/* Update the PE to FW */
	wmb();
	iowrite32be((capi->direct_pe << 16) | capi->default_pe,
		    &dev->iseg->direct_default_pe);
	mmiowb();

	mlx5_core_dbg(dev, "mlx5_capi_setup vsec=%04x direct_pe=%04x default_pe=%04x cxl_mode=%d\n",
			    capi->vsec,
			    capi->direct_pe,
			    capi->default_pe,
			    capi->cxl_mode);

out:
	return err;
}

/* Return 0 if
 *   the card is not CAPI capable
 *   the card is already in CAPI mode and the pci device is not function 0
 *   the CPU is not CAPI capable
 *   the card is in PCI mode and we don't want to do CAPI (from NVConfig)
 * Other case return -EPERM to tell our drivers to quit;
 */
int mlx5_capi_initialize(struct mlx5_core_dev *dev,
			 struct pci_dev *pdev)
{
	struct mlx5_priv      *priv;
	struct mlx5_capi_priv *capi;
	struct pci_dev        *capi_pdev;
	int                    err = 0;
	bool                   is_function0 = !PCI_FUNC(pdev->devfn);

	priv = &dev->priv;
	capi = &priv->capi;

	/* Set cxl_mode and default_pe to zero */
	capi->cxl_mode = 0;
	capi->default_pe = 0;
	
	/* Find mlx pci function 0 */
	if (is_function0)
		capi_pdev = pdev;
	else
		capi_pdev = mlx5_capi_find_function0_dev(pdev);

	if (!capi_pdev)
		goto out;

	/* Find CAPI vsec */
	capi->vsec = find_cxl_vsec(capi_pdev);
	if (!capi->vsec)
		goto out;

	/* Get cxl mode */
	if(mlx5_capi_get_capi_mode(capi_pdev, capi->vsec, &capi->cxl_mode))
		goto out;

	if (!capi->cxl_mode) {
		/* Huy to do Query CAPI switch capability before do wth switching */
		/* If cannot be switch, then continue to operate in PCIe mode */
		if (is_function0)
			/* If switch to CAPI fails, return 0 and stays in PCIe mode */
			if (cxl_check_and_switch_mode(pdev, CXL_BIMODE_CXL, 0, CXL_QUIRK_CX4)) {
				err = 0;
				goto out;
			}

		/* Quit our driver to move the card into CXL mode */
		err = -EPERM;
		goto out;
	} else {
		/* our driver does not control function 0 */
		if (is_function0) {
			err = -EPERM;
			goto out;
		}
	}

out:
	if (capi_pdev && (capi_pdev != pdev))
		pci_dev_put(capi_pdev);

	return err;
}
#endif
