#ifdef CONFIG_MLX5_CAPI 

#include <linux/mlx5/driver.h>
#include "mlx5_ib.h"
#include <linux/pci.h>
#include <misc/cxl.h>
#include <linux/sched.h>
#include <rdma/ib_umem.h> 

struct ib_umem *ib_umem_get_no_pin(struct ib_ucontext *context,
				   unsigned long addr,
				   size_t size,
				   int access)
{
	struct ib_umem *umem;

	umem = kzalloc(sizeof *umem, GFP_KERNEL);
	if (!umem)
		return ERR_PTR(-ENOMEM);

	umem->context   = context;
	umem->length    = size;
	umem->address   = addr;
	umem->page_size = PAGE_SIZE;
	umem->pid       = get_task_pid(current, PIDTYPE_PID);

	return umem;
}

void ib_umem_release_no_pin(struct ib_umem *umem)
{
	kfree(umem);
}

int mlx5_capi_get_default_pe_id(struct ib_pd *pd)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	
	return dev->mdev->priv.capi.default_pe;
}

int mlx5_capi_get_pe_id(struct ib_ucontext *ibcontext)
{
	struct mlx5_ib_ucontext *context = to_mucontext(ibcontext);

	return context->pe;
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
	}
	else {
		context->ctx = NULL;
		context->pe = 0;
	}

	mlx5_ib_dbg(dev, "Allocate pe_id %d\n", context->pe);
	return 0;
}

int mlx5_capi_release_cxl_context(struct ib_ucontext *ibcontext)
{
	struct mlx5_ib_ucontext *context = to_mucontext(ibcontext);

	if (context->ctx) {
		cxl_stop_context(context->ctx);
		cxl_release_context(context->ctx);
		context->pe = 0;
	}

	return 0;
}
#endif
