#ifdef CONFIG_MLX5_CAPI
static inline int calulate_npages_no_pin(u64 start, u64 length, int page_shift)
{
	int npages;
	u64 base;

	base = start & (~((1 << (page_shift)) -1));
	npages = (((start + length) - base) >> (page_shift));

	if (npages == 0)
		npages = 1;

	/* If span accross last page boundary, add a page */
	if ((start + length) > (base + (npages << page_shift)))
		npages ++;

	return npages;
}

struct ib_umem *ib_umem_get_no_pin(struct ib_ucontext *context,
				   unsigned long addr,
				   size_t size,
				   int access);
void ib_umem_release_no_pin(struct ib_umem *umem);
int mlx5_capi_get_default_pe_id(struct ib_pd *pd);
int mlx5_capi_get_pe_id(struct ib_ucontext *ibcontext);
int mlx5_capi_get_pe_id_from_pd(struct ib_pd *pd);
int mlx5_capi_allocate_cxl_context(struct ib_ucontext *ibcontext,
				   struct mlx5_ib_dev *dev);
int mlx5_capi_release_cxl_context(struct ib_ucontext *ibcontext);
#endif
