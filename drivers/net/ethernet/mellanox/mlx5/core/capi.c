#ifdef CONFIG_MLX5_CAPI

#include <linux/mlx5/driver.h>
#include <misc/cxl.h>
#include <linux/pci.h>

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

	if ((rc = CXL_READ_VSEC_MODE_CONTROL(dev, vsec, &val))) {
		dev_err(&dev->dev, "failed to read current mode control: %i", rc);
		return rc;	
	}

	printk ("mode control =%x\n", val);
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
	struct cxl_context    *capi_context;

	dev_info(&pdev->dev, "mlx5_capi_initialize entry, is_function0 %d\n", is_function0);
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
		/* FIXME add code to determine proper CPU before calling the switch */
		/* FIXME the card is in PCI mode and we don't want to do CAPI (from NVConfig) */
		if (is_function0)
			cxl_check_and_switch_mode(pdev, CXL_BIMODE_CXL, 0, CXL_QUIRK_CX4);
		
		/* Quit our driver to move the card into CXL mode */
		err = -EPERM;
		goto out;
	} else {
		/* our driver does not control function 0 */
		if (is_function0)
			err = -EPERM;
			goto out;

		capi_context = cxl_get_context(pdev);
		if (!capi_context) {
			dev_err(&pdev->dev, "No cxl context!\n");
			return -ENODEV;
		}

		capi->default_pe = cxl_process_element(capi_context);
		dev_info(&pdev->dev, "cxl pe: %d\n", capi->default_pe);

		cxl_start_context(capi_context, 0, NULL);
	}

out:
	dev_info(&pdev->dev, "vsec=%04x default_pe=%04x cxl_mode=%d err=%d\n",
		 capi->vsec, capi->default_pe, capi->cxl_mode, err);

	if (capi_pdev && (capi_pdev != pdev)) {
		printk("Clean up capi pdev\n");
		pci_dev_put(capi_pdev);
	}

	//return -EPERM;
	return err;
}
#endif
