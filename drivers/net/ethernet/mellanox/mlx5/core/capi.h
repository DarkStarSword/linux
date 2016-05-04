#ifdef CONFIG_MLX5_CAPI
#include <linux/mlx5/driver.h>
#include <misc/cxl.h>
#include <linux/pci.h>

/* Get cxl mode from mlx5_core_dev */
#define get_cxl_mode(dev) (dev->priv.capi.cxl_mode)

int mlx5_capi_initialize(struct mlx5_core_dev *dev, struct pci_dev *pdev);
int mlx5_capi_setup(struct mlx5_core_dev *dev, struct pci_dev *pdev);
int mlx5_capi_cleanup(struct mlx5_core_dev *dev,
		      struct pci_dev *pdev);
#endif
