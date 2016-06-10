#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/highmem.h>
#include <asm/fpu/api.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#ifndef CONFIG_X86
# error "This module only works on X86"
#endif

MODULE_LICENSE("GPL");

static int node = 1;
module_param(node, int, S_IRUGO);

static int use_dma = 0;
module_param(use_dma, int, S_IRUGO);

static int use_avx = 0;
module_param(use_avx, int, S_IRUGO);

static int page_order = 10;
module_param(page_order, int, S_IRUGO);

static int iterations = 8;
module_param(iterations, int, S_IRUGO);

static int use_multi_dma = 0;
module_param(use_multi_dma, int, S_IRUGO);



/*static bool is_tlb_flush = 1;*/
/*module_param(is_tlb_flush, bool, S_IRUGO);*/

/* LLC misses: 0x412e, STLB Flush: 0x20bd, DTLB Flush:0x01bd,
							 ITLB Flush: 0x01ae */

/*static struct perf_event_attr tlb_flush_event_attr = {*/
	/*.type           = PERF_TYPE_RAW,*/
	/*.config         = 0x412e, */
	/*.size           = sizeof(struct perf_event_attr),*/
	/*.pinned         = 1,*/
	/*.disabled       = 1,*/
/*};*/

static inline void copy_pages(struct page *to, struct page *from)
{
	char *vfrom, *vto;
	/*int i;*/

	vfrom = kmap_atomic(from);
	vto = kmap_atomic(to);
	copy_page(vto, vfrom);
	/*for (i = 0; i < 4096; i++)*/
		/*vto[i] = vfrom[i];*/
	kunmap_atomic(vto);
	kunmap_atomic(vfrom);
}
static inline void copy_pages_nocache(struct page *to, struct page *from)
{
	char *vfrom, *vto;
	int i;

	vfrom = kmap_atomic(from);
	vto = kmap_atomic(to);
	/*copy_page_nocache(vto, vfrom);*/



	if (boot_cpu_has(X86_FEATURE_AVX2)) {
		/*set_memory_wc((unsigned long)vfrom, 1);*/
		kernel_fpu_begin();
		//__asm__ __volatile__("mfence \n"::);
		for (i = 0; i < 4096/256; i++) {
			__asm__ __volatile__ (
			" vmovntdqa (%0), %%ymm0\n"
			" vmovntdq  %%ymm0, (%1)\n"
			" vmovntdqa 32(%0), %%ymm1\n"
			" vmovntdq  %%ymm1, 32(%1)\n"
			" vmovntdqa 64(%0), %%ymm2\n"
			" vmovntdq  %%ymm2, 64(%1)\n"
			" vmovntdqa 96(%0), %%ymm3\n"
			" vmovntdq  %%ymm3, 96(%1)\n"
			" vmovntdqa 128(%0), %%ymm4\n"
			" vmovntdq  %%ymm4, 128(%1)\n"
			" vmovntdqa 160(%0), %%ymm5\n"
			" vmovntdq  %%ymm5, 160(%1)\n"
			" vmovntdqa 192(%0), %%ymm6\n"
			" vmovntdq  %%ymm6, 192(%1)\n"
			" vmovntdqa 224(%0), %%ymm7\n"
			" vmovntdq  %%ymm7, 224(%1)\n"
				: : "r" (vfrom), "r" (vto) : "memory");
			vfrom += 256;
			vto += 256;
		}
		/*
		 * Since movntq is weakly-ordered, a "sfence" is needed to become
		 * ordered again:
		 */
		__asm__ __volatile__("sfence \n"::);
		kernel_fpu_end();
		/*set_memory_wb((unsigned long)vfrom, 1);*/
	} else if (boot_cpu_has(X86_FEATURE_AVX)) {
		kernel_fpu_begin();
		//__asm__ __volatile__("mfence \n"::);
		for (i = 0; i < 4096/128; i++) {
			__asm__ __volatile__ (
			" vmovntdqa (%0), %%xmm0\n"
			" vmovntdq  %%xmm0, (%1)\n"
			" vmovntdqa 16(%0), %%xmm1\n"
			" vmovntdq  %%xmm1, 16(%1)\n"
			" vmovntdqa 32(%0), %%xmm2\n"
			" vmovntdq  %%xmm2, 32(%1)\n"
			" vmovntdqa 48(%0), %%xmm3\n"
			" vmovntdq  %%xmm3, 48(%1)\n"
			" vmovntdqa 64(%0), %%xmm4\n"
			" vmovntdq  %%xmm4, 64(%1)\n"
			" vmovntdqa 80(%0), %%xmm5\n"
			" vmovntdq  %%xmm5, 80(%1)\n"
			" vmovntdqa 96(%0), %%xmm6\n"
			" vmovntdq  %%xmm6, 96(%1)\n"
			" vmovntdqa 112(%0), %%xmm7\n"
			" vmovntdq  %%xmm7, 112(%1)\n"
				: : "r" (vfrom), "r" (vto) : "memory");
			vfrom += 128;
			vto += 128;
		}
		/*
		 * Since movntq is weakly-ordered, a "sfence" is needed to become
		 * ordered again:
		 */
		__asm__ __volatile__("sfence \n"::);
		kernel_fpu_end();
	} else {
		pr_info("use copy_page");
		copy_page(vto, vfrom);
	}
	kunmap_atomic(vto);
	kunmap_atomic(vfrom);
}

static int __init bench_init(void)
{
	/*volatile char * from;*/
	/*volatile char * to;*/
	struct page* start_page[32] = {0};
	struct page* end_page[32] = {0};
	char *third_party = NULL;
	int cpu;
	int i, j;
	unsigned long k;
	ulong irqs;
	unsigned long idx;
	u64 begin = 0, end = 0;

	/*struct perf_event *tlb_flush;*/
	char *vpage;
	struct dma_chan *copy_chan[16] = {0};
	struct dma_device *device[16] = {0};
	struct dma_async_tx_descriptor *tx[16] = {0};
	dma_cookie_t cookie[16];
	enum dma_ctrl_flags dma_flags[16] = {0};
	struct dmaengine_unmap_data *unmap[16] = {0};
	dma_cap_mask_t mask[16];

	/*u64 tlb_flushes_1, tlb_flushes_2, tlb_flushes_3, enabled, running;*/

	if (page_order < 0)
		page_order = 0;
	if (page_order > 10)
		page_order = 10;

	if (iterations < 1)
		iterations = 1;
	if (iterations > 32)
		iterations = 32;


	for (i = 0; i < iterations; ++i) {
		start_page[i] = __alloc_pages_node(1, (GFP_HIGHUSER_MOVABLE |
									  __GFP_THISNODE), page_order);

		if (!start_page[i]) {
			pr_err("fail to allocate start page in iteration: %d", i);
			goto out_page;
		}
		end_page[i] = __alloc_pages_node(0, (GFP_HIGHUSER_MOVABLE |
									  __GFP_THISNODE), page_order);
		if (!end_page[i]) {
			pr_err("fail to allocate end page in iteration: %d", i);
			goto out_page;
		}
	}
	pr_info("start page: %d, end_page: %d, vmalloc: %d", page_to_nid(start_page[0]), page_to_nid(end_page[0]), node);

	third_party = vmalloc_node(32UL*1024*1024, node);
	if (!third_party) {
		pr_err("cannot get third party");
		goto out_page;
	}
	for (i = 0; i < iterations; ++i) {
		vpage = kmap_atomic(start_page[i]);
		
		memset(vpage, 0, PAGE_SIZE<<page_order);

		for (k = 0; k < PAGE_SIZE<<page_order; k += PAGE_SIZE)
			vpage[k] = i;

		kunmap_atomic(vpage);
	}

	if (use_dma) {
		if (use_multi_dma) {

		} else {
			dma_cap_zero(mask[0]);
			dma_cap_set(DMA_MEMCPY, mask[0]);
			dmaengine_get();

			if (!copy_chan[0])
				copy_chan[0] = dma_request_channel(mask[0], NULL, NULL);

			device[0] = copy_chan[0]->device;
			
			unmap[0] = dmaengine_get_unmap_data(device[0]->dev, iterations*2, GFP_NOWAIT);
		}

	} else {
		/*for (i = 0; i < iterations; ++i) {*/
			/*vpage = kmap_atomic(start_page[i]);*/
			/*set_memory_wc((unsigned long)vpage, 1<<page_order);*/
			/*kunmap_atomic(vpage);*/
		/*}*/
	}


	get_random_bytes((void*)third_party, 32UL*1024*1024);

	/* Disable interrupts */
	cpu = get_cpu();
	/* Setup TLB miss, and cache miss counters */
	/*tlb_flush = perf_event_create_kernel_counter(&tlb_flush_event_attr,*/
		/*cpu, NULL, NULL, NULL);*/
	/*if (IS_ERR(tlb_flush)) {*/
		/*pr_err("Failed to create kernel counter\n");*/
		/*goto out_putcpu;*/
	/*}*/

	pr_info("get_cpu: %d\n", cpu);

	/*perf_event_enable(tlb_flush);*/

	local_irq_save(irqs);
	/* Read TLB miss and Cache miss counters */
	/*tlb_flushes_1 = perf_event_read_value(tlb_flush, &enabled, &running);*/


	if (use_dma) {
		if (use_multi_dma) {

		} else {
			begin = rdtsc();
			unmap[0]->to_cnt = iterations;

			for (i = 0; i < iterations; ++i) {
				unmap[0]->addr[i] = dma_map_page(device[0]->dev, end_page[i], 0, 
											  PAGE_SIZE<<page_order, DMA_TO_DEVICE);
			}

			unmap[0]->from_cnt = iterations;
			for (; i < iterations*2; ++i) {
				unmap[0]->addr[i] = dma_map_page(device[0]->dev, start_page[i-iterations], 0, 
											  PAGE_SIZE<<page_order, DMA_FROM_DEVICE);
			}
			unmap[0]->len = PAGE_SIZE<<page_order;


			for (i = 0; i < iterations; ++i) {
				tx[0] = device[0]->device_prep_dma_memcpy(copy_chan[0], unmap[0]->addr[i],
									unmap[0]->addr[i+iterations], unmap[0]->len, dma_flags[0]);
				if (!tx[0]) {
					pr_err("Zi: no tx descriptor");
					break;
				}
				cookie[0] = tx[0]->tx_submit(tx[0]);

				if (dma_submit_error(cookie[0])) {
					pr_err("Zi: submit error");
					break;
				}

				if (dma_sync_wait(copy_chan[0], cookie[0]) != DMA_COMPLETE) {
					pr_err("Zi: dma did not complete");
				}

			}

			end = rdtsc();
		}

	} else {
		begin = rdtsc();
		for (i = 0; i < iterations; ++i) {
			for (j = 0; j < 1<<page_order; ++j)
				if (use_avx)
					copy_pages_nocache(end_page[i]+j, start_page[i]+j);
				else
					copy_pages(end_page[i]+j, start_page[i]+j);
		}
		end = rdtsc();
	}



	/* Read the counters again */
	/*tlb_flushes_2 = perf_event_read_value(tlb_flush, &enabled, &running);*/


	for (idx = 16UL*1024*1024; idx < 32UL*1024*1024; idx++) {
		third_party[idx] = idx % 10;
	}

	/* Read the counters again */
	/*tlb_flushes_3 = perf_event_read_value(tlb_flush, &enabled, &running);*/

	/* Print results */
	/*pr_info("Page migration LLC misses: %llu (%llu - %llu)\n",*/
		/*tlb_flushes_2 - tlb_flushes_1,*/
		/*tlb_flushes_2, tlb_flushes_1);*/
	/* Print results */
	/*pr_info("Third party data LLC misses: %llu (%llu - %llu)\n",*/
		/*tlb_flushes_3 - tlb_flushes_2,*/
		/*tlb_flushes_3, tlb_flushes_2);*/

	pr_info("Page copy time: %llu cycles, %llu microsec", (end - begin), (end - begin)/2600);



	local_irq_restore(irqs);

	if (use_dma) {
		if (use_multi_dma) {

		} else {
			dmaengine_unmap_put(unmap[0]);
			if (copy_chan[0]) {
				dma_release_channel(copy_chan[0]);
				copy_chan[0] = NULL;
			}
			dmaengine_put();
		}
	}

	/* Clean up counters */
	/*perf_event_disable(tlb_flush);*/

	/*perf_event_release_kernel(tlb_flush);*/

	for (i = 0; i < iterations; ++i) {
		vpage = kmap_atomic(end_page[i]);

		for (k = 0; k < PAGE_SIZE<<page_order; k += PAGE_SIZE) {
			if (vpage[k] != i)
				pr_err("iterations: %d, page offset %lu corrupted", i, k);
		}

		/*set_memory_wb((unsigned long)vpage, 1<<page_order);*/
		kunmap_atomic(vpage);
	}
/*out_putcpu:*/
	/* Enable interrupts */
	put_cpu();
/*out_unmappte:*/
/*out_putmm:*/
	vfree(third_party);

out_page:
	for (i = 0; i < iterations; ++i) {
		if (end_page[i])
			__free_pages(end_page[i], page_order);
		if (start_page[i])
			__free_pages(start_page[i], page_order);
	}
	return 0;
}

static void __exit bench_exit(void)
{
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
