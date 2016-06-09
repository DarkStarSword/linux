#ifdef CONFIG_MLX5_CAPI 

#include <linux/mlx5/driver.h>
#include "mlx5_ib.h"
#include <linux/pci.h>
#include <misc/cxl.h>
#include <linux/sched.h>
#include <rdma/ib_umem.h> 

static inline u64 mlx5_ib_dma_map_single(struct ib_device *dev,
					 void *cpu_addr, size_t size,
					 enum dma_data_direction direction)
{
	return (u64)cpu_addr;
}

static inline void mlx5_ib_dma_unmap_single(struct ib_device *dev,
					    u64 addr, size_t size,
					    enum dma_data_direction direction)
{
}

static inline int mlx5_ib_dma_mapping_error(struct ib_device *dev, u64 dma_addr)
{
	return 0;
}

static inline u64 mlx5_ib_dma_map_page(struct ib_device *dev,
				       struct page *page,
				       unsigned long offset,
				       size_t size,
				       enum dma_data_direction direction)
{
	return (u64)(page_address(page)) + offset;	
}

static inline void mlx5_ib_dma_unmap_page(struct ib_device *dev,
					  u64 addr, size_t size,
					  enum dma_data_direction direction)
{
}

static int mlx5_ib_dma_map_sg(struct ib_device *dev, struct scatterlist *sgl,
			      int nents, enum dma_data_direction direction)
{
	struct scatterlist *sg;
	u64 addr;
	int i;
	int ret = nents;

	for_each_sg(sgl, sg, nents, i) {
		addr = (u64) page_address(sg_page(sg));
		sg->dma_address = addr + sg->offset;
		sg->dma_length = sg->length;
	}
	return ret;
}

static void mlx5_ib_dma_unmap_sg(struct ib_device *dev,
				 struct scatterlist *sg, int nents,
				 enum dma_data_direction direction)
{
}

static void mlx5_ib_dma_sync_single_for_cpu(struct ib_device *dev, u64 addr,
					    size_t size, enum dma_data_direction dir)
{
}

static void mlx5_ib_dma_sync_single_for_device(struct ib_device *dev, u64 addr,
					       size_t size,
					       enum dma_data_direction dir)
{
	return;
}

static void *mlx5_ib_dma_alloc_coherent(struct ib_device *dev, size_t size,
					u64 *dma_handle, gfp_t flag)
{
	struct page *p;
	void *addr = NULL;

	p = alloc_pages(flag, get_order(size));
	if (p)
		addr = page_address(p);
	if (dma_handle)
		*dma_handle = (u64) addr;
	return addr;
}

static void mlx5_ib_dma_free_coherent(struct ib_device *dev, size_t size,
				      void *cpu_addr, u64 dma_handle)
{
	free_pages((unsigned long) cpu_addr, get_order(size));
}

/* dma_ops when the card is in CAPI mode */
struct ib_dma_mapping_ops mlx5_dma_mapping_ops = {
	.mapping_error = mlx5_ib_dma_mapping_error,
	.map_single = mlx5_ib_dma_map_single,
	.unmap_single = mlx5_ib_dma_unmap_single,
	.map_page = mlx5_ib_dma_map_page,
	.unmap_page = mlx5_ib_dma_unmap_page,
	.map_sg = mlx5_ib_dma_map_sg,
	.unmap_sg = mlx5_ib_dma_unmap_sg,
	.sync_single_for_cpu = mlx5_ib_dma_sync_single_for_cpu,
	.sync_single_for_device = mlx5_ib_dma_sync_single_for_device,
	.alloc_coherent = mlx5_ib_dma_alloc_coherent,
	.free_coherent = mlx5_ib_dma_free_coherent
};

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
