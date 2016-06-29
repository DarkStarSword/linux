#ifdef CONFIG_MLX5_CAPI
#include <linux/mlx5/driver.h>
#include <misc/cxl.h>
#include <linux/pci.h>

void mlx5_configure_msix_table_capi(struct pci_dev *pdev);
int mlx5_capi_initialize(struct mlx5_core_dev *dev, struct pci_dev *pdev);
int mlx5_capi_setup(struct mlx5_core_dev *dev, struct pci_dev *pdev);
int mlx5_capi_cleanup(struct mlx5_core_dev *dev,
		      struct pci_dev *pdev);
#endif
