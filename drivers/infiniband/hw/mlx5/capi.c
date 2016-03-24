#ifdef CONFIG_MLX5_CAPI 

#include <linux/mlx5/driver.h>
#include "mlx5_ib.h"
#include <linux/pci.h>
#include <misc/cxl.h>

int mlx5_capi_get_default_pe_id(struct ib_pd *pd)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	
	return dev->mdev->priv.capi.default_pe;
}

int mlx5_capi_get_pe_id(struct ib_ucontext *ibcontext)
{
	struct mlx5_ib_ucontext *context = to_mucontext(ibcontext);

	printk("mlx5_capi_get_pe_id %d\n", context->pe);
	return context->pe;
}

int mlx5_capi_allocate_cxl_context(struct ib_ucontext *ibcontext,
				   struct mlx5_ib_dev *dev)
{
	struct mlx5_ib_ucontext *context = to_mucontext(ibcontext);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_capi_priv *capi = &mdev->priv.capi; 

	/* FIXME Phase 1 use default PE-ID */
	if (capi->cxl_mode)
		context->pe = PCI_FUNC(mdev->pdev->devfn);
	else
		context->pe = 0;
	return 0;
}
#endif
