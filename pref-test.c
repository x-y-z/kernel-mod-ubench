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
#include <linux/workqueue.h>
#include <linux/slab.h>

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

#if 1
struct dma_transfer_breakdown {
	unsigned long long dmaengine_get_cycles;
	unsigned long long find_dma_chan_cycles;
	unsigned long long get_unmap_data_cycles;
	unsigned long long map_pages_cycles;
	unsigned long long prep_dma_memcpy_cycles;
	unsigned long long tx_submit_cycles;
	unsigned long long dma_sync_wait_cycles;
	unsigned long long put_unmap_data_cycles;
	unsigned long long release_dma_chan_cycles;
	unsigned long long dmaengine_put_cycles;
	unsigned long total_counts;
};
#endif

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

struct dma_work_item {
	struct work_struct my_work;

	struct dma_chan *copy_chan;
	struct dma_device *device;
	struct dma_async_tx_descriptor *tx;
	dma_cookie_t *cookie;
	enum dma_ctrl_flags dma_flags;
	struct dmaengine_unmap_data *unmap;
	
};

void dma_work_func(struct work_struct *work)
{
	struct dma_work_item *mywork = (struct dma_work_item*)work;

	struct dma_chan *copy_chan = mywork->copy_chan;
	struct dma_device *device = mywork->device;
	struct dma_async_tx_descriptor *tx = NULL;
	dma_cookie_t *cookie = mywork->cookie;
	enum dma_ctrl_flags dma_flags = 0;
	struct dmaengine_unmap_data *unmap = mywork->unmap;


	tx = device->device_prep_dma_memcpy(copy_chan, unmap->addr[0],
						unmap->addr[1], unmap->len, dma_flags);
	if (!tx) {
		pr_err("Zi: no tx descriptor");
		return;
	}
	*cookie = tx->tx_submit(tx);

	if (dma_submit_error(*cookie)) {
		pr_err("Zi: submit error");
		return;
	}

}

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
	/*ulong irqs;*/
	/*unsigned long idx;*/
	u64 begin = 0, end = 0;
	/*u64 time_map = 0, time_prepare = 0, time_submit = 0, time_wait = 0;*/
	/*u64 time_wait_all[16] = {0};*/
	/*u64 time_tmp1, time_tmp2;*/
	u64 copy_page_begin = 0, copy_page_end = 0;

	struct dma_transfer_breakdown dma_transfer_breakdown = {0};

	/*struct perf_event *tlb_flush;*/
	char *vpage, *vpage2;
	struct dma_chan *copy_chan[16] = {0};
	struct dma_device *device[16] = {0};
	dma_cookie_t *cookie = NULL;
	struct dmaengine_unmap_data **unmap = NULL;
	dma_cap_mask_t mask;
	int iterations_per_channel = 1;
	/*int channels_per_iteration = 1;*/
	int chan_iter;
	size_t in_page_offset = 0;
	size_t chunk_size = 0;
	int page_index = 0;
	
	int total_requests = 0;

	struct dma_work_item *work_list = NULL;

	/*u64 tlb_flushes_1, tlb_flushes_2, tlb_flushes_3, enabled, running;*/

	/*if (page_order < 0)*/
		/*page_order = 0;*/
	/*if (page_order > 10)*/
		/*page_order = 10;*/

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
	pr_info("page size: %lu", PAGE_SIZE<<page_order);

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
		vpage2 = kmap_atomic(end_page[i]);
		memset(vpage2, 0, PAGE_SIZE<<page_order);
		kunmap_atomic(vpage2);
	}


	if (use_dma) {

		begin = rdtsc();

		dma_cap_zero(mask);
		dma_cap_set(DMA_MEMCPY, mask);
		dmaengine_get();

		end = rdtsc();
		dma_transfer_breakdown.dmaengine_get_cycles = end - begin;

		begin = rdtsc();
		for (i = 0; i < use_multi_dma; ++i) 
		{
			copy_chan[i] = dma_request_channel(mask, NULL, NULL);
			
			if (!copy_chan[i])
				return -ENODEV;

			device[i] = copy_chan[i]->device;
		}

		end = rdtsc();
		dma_transfer_breakdown.find_dma_chan_cycles = end - begin;

		total_requests = max_t(int, iterations, use_multi_dma);
				
		if (iterations >= use_multi_dma)	
			iterations_per_channel = iterations / use_multi_dma;
		else {
			iterations_per_channel = 1;
			chunk_size = iterations * (PAGE_SIZE<<page_order) / use_multi_dma;
		}

		work_list = kzalloc(sizeof(struct dma_work_item) * total_requests, GFP_NOWAIT);


		pr_info("Total request: %d, iterations: %d, use_multi_dma: %d\n",
				total_requests, iterations, use_multi_dma);
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

	/*local_irq_save(irqs);*/
	/* Read TLB miss and Cache miss counters */
	/*tlb_flushes_1 = perf_event_read_value(tlb_flush, &enabled, &running);*/


	if (use_dma) {
				
		unmap = kzalloc(sizeof(struct dmaengine_unmap_data*) * total_requests, GFP_NOWAIT);
		cookie = kzalloc(sizeof(dma_cookie_t) * total_requests, GFP_NOWAIT);

		begin = rdtsc();
		for (i = 0; i < total_requests; i++)
		{
			chan_iter = total_requests % use_multi_dma;
			unmap[i] = dmaengine_get_unmap_data(device[chan_iter]->dev, 
												2,
												GFP_NOWAIT);
		}
		end = rdtsc();
		dma_transfer_breakdown.get_unmap_data_cycles = end - begin;

		copy_page_begin = begin = rdtsc();

		if (iterations >= use_multi_dma)
		{

			if (iterations % use_multi_dma != 0)
			{
				pr_err("some pages will not be transferred because iterations is not multple of use_multi_dma\n");
			}

			for (i = 0; i < total_requests; i++)
			{
				chan_iter = i % use_multi_dma;

				begin = rdtsc();

				unmap[i]->to_cnt = 1;

				unmap[i]->addr[0] = dma_map_page(device[chan_iter]->dev, end_page[i], 0, 
											  PAGE_SIZE<<page_order, DMA_FROM_DEVICE);

				unmap[i]->from_cnt = 1;
				unmap[i]->addr[1] = dma_map_page(device[chan_iter]->dev, start_page[i], 0, 
											  PAGE_SIZE<<page_order, DMA_TO_DEVICE);
				unmap[i]->len = PAGE_SIZE<<page_order;

				end = rdtsc();
				dma_transfer_breakdown.map_pages_cycles += end - begin;


				INIT_WORK((struct work_struct*)&work_list[i], dma_work_func);

				work_list[i].copy_chan = copy_chan[chan_iter];
				work_list[i].device = device[chan_iter];
				work_list[i].unmap = unmap[i];
				work_list[i].cookie = &cookie[i];

				queue_work(system_highpri_wq, (struct work_struct*)&work_list[i]);
			}
		}
		else /* iterations < use_multi_dma  */
		{
			if (use_multi_dma % iterations != 0)
			{
				pr_err("not all pages will be transferred, iterations is not a multiplier of use_multi_dma\n");
			}

			in_page_offset = 0;
			page_index = 0;
			for (i = 0; i < total_requests; i++)
			{
				chan_iter = i % use_multi_dma;

				begin = rdtsc();

				unmap[i]->to_cnt = 1;

				unmap[i]->addr[0] = dma_map_page(device[chan_iter]->dev, end_page[page_index], in_page_offset, 
											  PAGE_SIZE<<page_order, DMA_FROM_DEVICE);

				unmap[i]->from_cnt = 1;
				unmap[i]->addr[1] = dma_map_page(device[chan_iter]->dev, start_page[page_index], in_page_offset, 
											  PAGE_SIZE<<page_order, DMA_TO_DEVICE);
				unmap[chan_iter]->len = chunk_size;

				in_page_offset = in_page_offset + chunk_size;

				if (in_page_offset >= (PAGE_SIZE<<page_order)) {
					in_page_offset = in_page_offset % (PAGE_SIZE<<page_order);

					page_index += 1;
				}

				end = rdtsc();
				dma_transfer_breakdown.map_pages_cycles += end - begin;

				INIT_WORK((struct work_struct*)&work_list[i], dma_work_func);

				work_list[i].copy_chan = copy_chan[chan_iter];
				work_list[i].device = device[chan_iter];
				work_list[i].unmap = unmap[i];
				work_list[i].cookie = &cookie[i];

				queue_work(system_highpri_wq, (struct work_struct*)&work_list[i]);

			}
		}

		flush_workqueue(system_highpri_wq);

		begin = rdtsc();
		for (i = 0; i < total_requests; ++i) {
			/* wait for them to finish  */
			chan_iter = i % use_multi_dma;
			if (dma_sync_wait(copy_chan[chan_iter], cookie[i]) != DMA_COMPLETE) {
				pr_err("Zi: dma did not complete");
			}
		}
		
		copy_page_end = end = rdtsc();
		dma_transfer_breakdown.dma_sync_wait_cycles = end - begin;

			
	} else {
		copy_page_begin = rdtsc();
		for (i = 0; i < iterations; ++i) {
			for (j = 0; j < 1<<page_order; ++j)
				if (use_avx)
					copy_pages_nocache(end_page[i]+j, start_page[i]+j);
				else
					copy_pages(end_page[i]+j, start_page[i]+j);
		}
		copy_page_end = rdtsc();
	}

	pr_info("Page copy time: %llu cycles, %llu microsec", 
			(copy_page_end - copy_page_begin), 
			(copy_page_end - copy_page_begin)/2600);


	for (i = 0; i < iterations; ++i) {
		vpage = kmap_atomic(end_page[i]);

		for (k = 0; k < PAGE_SIZE<<page_order; k += PAGE_SIZE) {
			if (vpage[k] != i)
				pr_err("iterations: %d, page offset %lu corrupted", i, k);
		}

		/*set_memory_wb((unsigned long)vpage, 1<<page_order);*/
		kunmap_atomic(vpage);
	}

	if (use_dma) {

		begin = rdtsc();
		for (i = 0; i < total_requests; ++i)
			dmaengine_unmap_put(unmap[i]);
		for (chan_iter = 0; chan_iter < use_multi_dma; ++chan_iter) {
			if (copy_chan[chan_iter]) {
				dma_release_channel(copy_chan[chan_iter]);
				copy_chan[chan_iter] = NULL;
			}
		}
		
		end = rdtsc();
		dma_transfer_breakdown.put_unmap_data_cycles = end - begin;
		begin = end;

		dmaengine_put();

		end = rdtsc();
		dma_transfer_breakdown.dmaengine_put_cycles = end - begin;

		if (unmap)
			kfree(unmap);
		if (cookie)
			kfree(cookie);
		if (work_list)
			kfree(work_list);
	}

	pr_info("Breakdown : dmaengine_get: %llu cycles, %llu microsec", 
		dma_transfer_breakdown.dmaengine_get_cycles , 
		dma_transfer_breakdown.dmaengine_get_cycles/2600);
	pr_info("Breakdown : find dma chan : %llu cycles, %llu microsec", 
		dma_transfer_breakdown.find_dma_chan_cycles, 
		dma_transfer_breakdown.find_dma_chan_cycles/2600);
	pr_info("Breakdown : get unmap data: %llu cycles, %llu microsec", 
		dma_transfer_breakdown.get_unmap_data_cycles, 
		dma_transfer_breakdown.get_unmap_data_cycles/2600);

	pr_info("Breakdown : map: %llu cycles, %llu microsec", 
		dma_transfer_breakdown.map_pages_cycles, 
		dma_transfer_breakdown.map_pages_cycles/2600);
	pr_info("Breakdown : prepare: %llu cycles, %llu microsec", 
		dma_transfer_breakdown.prep_dma_memcpy_cycles, 
		dma_transfer_breakdown.prep_dma_memcpy_cycles/2600);
	pr_info("Breakdown : submit: %llu cycles, %llu microsec", 
		dma_transfer_breakdown.tx_submit_cycles, 
		dma_transfer_breakdown.tx_submit_cycles/2600);
	pr_info("Breakdown : wait: %llu cycles, %llu microsec", 
		dma_transfer_breakdown.dma_sync_wait_cycles, 
		dma_transfer_breakdown.dma_sync_wait_cycles/2600);

	pr_info("Breakdown : put unmap data & release dma chan: %llu cycles, %llu microsec", 
		dma_transfer_breakdown.put_unmap_data_cycles, 
		dma_transfer_breakdown.put_unmap_data_cycles/2600);
	pr_info("Breakdown : dmaengine_put: %llu cycles, %llu microsec", 
		dma_transfer_breakdown.dmaengine_put_cycles , 
		dma_transfer_breakdown.dmaengine_put_cycles/2600);

	/*for (chan_iter = 0; chan_iter < use_multi_dma; ++chan_iter) {*/
		/*pr_info("Channel: %d, wait: %llu cycles, %llu microsec", chan_iter, time_wait_all[chan_iter], time_wait_all[chan_iter]/2600);*/
	/*}*/
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
