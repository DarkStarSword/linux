#ifdef CONFIG_MLX5_CAPI 

#include <linux/mlx5/driver.h>
#include "mlx5_ib.h"
#include <linux/pci.h>
#include <misc/cxl.h>
#include <linux/sched.h>

int mlx5_capi_get_default_pe_id(struct ib_pd *pd)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	
	return dev->mdev->priv.capi.default_pe;
}

int mlx5_capi_get_pe_id(struct ib_ucontext *ibcontext)
{
	struct mlx5_ib_ucontext *context = to_mucontext(ibcontext);

	printk(KERN_ALERT "mlx5_capi_get_pe_id %d\n", context->pe);
	return context->pe;
}

int mlx5_capi_get_pe_id2(struct ib_ucontext *ibcontext)
{
	struct mlx5_ib_ucontext *context = to_mucontext(ibcontext);

	printk(KERN_ALERT "mlx5_capi_get_pe_id for mkey%d\n", context->pe2);
	return context->pe2;
}

int mlx5_capi_allocate_cxl_context(struct ib_ucontext *ibcontext,
				   struct mlx5_ib_dev *dev)
{
	struct mlx5_ib_ucontext *context = to_mucontext(ibcontext);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct pci_dev *pdev = mdev->pdev;

	if (get_cxl_mode(mdev)) {
		context->ctx = cxl_dev_context_init(pdev);
		context->pe = cxl_process_element(context->ctx);
		cxl_start_context(context->ctx, 0, current);

		context->ctx2 = cxl_dev_context_init(pdev);
		context->pe2 = cxl_process_element(context->ctx2);
		cxl_start_context(context->ctx2, 0, current);

		
	}
	else {
		context->ctx = NULL;
		context->pe = 0;
	}

	printk(KERN_ALERT "mlx5_capi_allocate_cxl_context pe_id %d\n", context->pe);	
	printk(KERN_ALERT "mlx5_capi_allocate_cxl_context pe_id for mkey %d\n", context->pe2);

	return 0;
}

int mlx5_capi_release_cxl_context(struct ib_ucontext *ibcontext)
{
	struct mlx5_ib_ucontext *context = to_mucontext(ibcontext);
	
	return 0;

	if (context->ctx) {
		cxl_stop_context(context->ctx);
		cxl_release_context(context->ctx);
		context->pe = 0;
	}

	printk(KERN_ALERT "mlx5_capi_release_cxl_context\n");

	return 0;
}
#endif
