#ifdef CONFIG_MLX5_CAPI
#include <linux/mlx5/driver.h>
#include <misc/cxl.h>
#include <linux/pci.h>

int mlx5_capi_initialize(struct mlx5_core_dev *dev, struct pci_dev *pdev);
#endif
