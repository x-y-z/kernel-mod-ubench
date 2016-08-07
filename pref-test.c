#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_X86
#include <asm/fpu/api.h>
#endif

#ifdef CONFIG_PPC64
#include <asm/time.h>
#endif


MODULE_LICENSE("GPL");

static int node = 0;
module_param(node, int, S_IRUGO);

static uint nthreads = 4;
module_param(nthreads, uint, S_IRUGO);

static uint page_order= 10;
module_param(page_order, uint, S_IRUGO);

static uint batch = 1;
module_param(batch, uint, S_IRUGO);

/*static bool is_tlb_flush = 1;*/
/*module_param(is_tlb_flush, bool, S_IRUGO);*/

static struct page** start_page = NULL;
static struct page** end_page = NULL;


static u64 *begin_timestamps = NULL;
static unsigned int *cpu_id = NULL;

static int thread_id[32] = {0};


struct copy_page_info {
	struct work_struct copy_page_work;
	char *to;
	char *from;
	unsigned long chunk_size;
};


#ifdef CONFIG_X86

static noinline void _memcpy(void *to, void *from, size_t n)
{
	unsigned long d0, d1, d2;
	asm volatile("rep ; movsl\n\t"
		     "testb $2,%b4\n\t"
		     "je 1f\n\t"
		     "movsw\n"
		     "1:\ttestb $1,%b4\n\t"
		     "je 2f\n\t"
		     "movsb\n"
		     "2:"
		     : "=&c" (d0), "=&D" (d1), "=&S" (d2)
		     : "0" (n / 4), "q" (n), "1" ((long)to), "2" ((long)from)
		     : "memory");
	return;
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
/*static inline void copy_pages_nocache(struct page *to, struct page *from)*/
static inline void copy_pages_nocache(char *vto, char *vfrom)
{
	/*char *vfrom, *vto;*/
	int i;

	/*vfrom = kmap_atomic(from);*/
	/*vto = kmap_atomic(to);*/
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
	/*kunmap_atomic(vto);*/
	/*kunmap_atomic(vfrom);*/
}
#endif

#if 0
u64 rdtsc(void)
{
    return get_tb();
}
#endif

void copy_page_thread(struct work_struct *work)
{
	struct copy_page_info *my_work = (struct copy_page_info*)work;
	unsigned long i;
#ifdef CONFIG_X86
	_memcpy(my_work->to,
			my_work->from,
			my_work->chunk_size);
#endif

#ifdef CONFIG_PPC64
	memcpy(my_work->to,
			my_work->from,
			my_work->chunk_size);
        //for (i = 0; i < my_work->chunk_size; i++) {
	//	my_work->to[i] = my_work->from[i];
	//}
#endif
}

static int __init bench_init(void)
{
	int i, j;
	u64 timestamp;
	u64 duration;
	const struct cpumask *cpumask = cpumask_of_node(node);
	char *vpage;
	char *vfrom, *vto;
	unsigned long k;
	int num_work_item = 0;
	unsigned long chunk_size = (PAGE_SIZE<<page_order)/nthreads;
	struct copy_page_info *work_items;
        char *pollution;
	unsigned long pollution_size = 32UL*1024*1024;

	/*batch = nthreads;*/

	pollution = alloc_pages_exact(pollution_size,GFP_KERNEL);

	if (!pollution)
	    goto out_free_timestamp;


	cpu_id = kmalloc_node(sizeof(unsigned int)*nthreads, GFP_KERNEL, node);

	cpu_id[0] = cpumask_first(cpumask);

	for (i = 1; i < nthreads; i++) {
		thread_id[i] = i;
		cpu_id[i] = cpumask_next(cpu_id[i-1], cpumask);
		for (j = 0; j < 7; j++)
		    cpu_id[i] = cpumask_next(cpu_id[i], cpumask);

		if (cpu_id[i] > nr_cpu_ids)
			goto out_free_timestamp;
	}

	for (i = 0; i < nthreads; i++) {
		pr_info("Thread: %d, at cpu: %d\n", i, cpu_id[i]);
	}

	start_page = kmalloc_node(sizeof(struct page*)*batch, GFP_KERNEL, node);
	if (!start_page)
		goto out_free_page_list;

	end_page = kmalloc_node(sizeof(struct page*)*batch, GFP_KERNEL, node);
	if (!end_page)
		goto out_free_page_list;


	for (i = 0; i < batch; ++i) {
		start_page[i] = __alloc_pages_node(1, (GFP_KERNEL|
									  __GFP_THISNODE) & ~__GFP_RECLAIM, page_order);

		if (!start_page[i]) {
			pr_err("fail to allocate start page in iteration: %d", i);
			goto out_page;
		}
		end_page[i] = __alloc_pages_node(0, (GFP_KERNEL|
									  __GFP_THISNODE) & ~__GFP_RECLAIM, page_order);
		if (!end_page[i]) {
			pr_err("fail to allocate end page in iteration: %d", i);
			goto out_page;
		}
	}

	pr_info("start page: %d, end_page: %d, cpus: %d, size: %ld\n", page_to_nid(start_page[0]), page_to_nid(end_page[0]), node, PAGE_SIZE);

	for (i = 0; i < batch; ++i) {
		unsigned long start;
#ifdef CONFIG_X86
		vpage = kmap_atomic(start_page[i]);
#endif
		
#ifdef CONFIG_PPC64
		vpage = kmap(start_page[i]);
#endif
		memset(vpage, 0, PAGE_SIZE<<page_order);

		for (k = 0; k < PAGE_SIZE<<page_order; k += PAGE_SIZE)
			vpage[k] = i+1;
#ifdef CONFIG_X86
		clflush_cache_range(vpage, PAGE_SIZE<<page_order);
#endif

#ifdef CONFIG_PPC64
		//__dma_sync(vpage, PAGE_SIZE<<page_order, 0);
		//for (start = 0; start < (1<<page_order); start++) {
		//	struct page *one_page = start_page[i] + start;
		//	flush_dcache_icache_page(one_page);
		//}
#endif

#ifdef CONFIG_X86
		kunmap_atomic(vpage);
		vpage = kmap_atomic(end_page[i]);
#endif

#ifdef CONFIG_PPC64
		kunmap(start_page[i]);
		vpage = kmap(end_page[i]);
#endif
		memset(vpage, 0, PAGE_SIZE<<page_order);

#ifdef CONFIG_X86
		clflush_cache_range(vpage, PAGE_SIZE<<page_order);
#endif

#ifdef CONFIG_PPC64
		//__dma_sync(vpage, PAGE_SIZE<<page_order, 0);
		//for (start = 0; start < (1<<page_order); start++) {
		//	struct page *one_page = end_page[i] + start;
		//	flush_dcache_icache_page(one_page);
		//}
#endif


#ifdef CONFIG_X86
		kunmap_atomic(vpage);
#endif
#ifdef CONFIG_PPC64
		kunmap(end_page[i]);
#endif
	}

	get_random_bytes(pollution, pollution_size);

	num_work_item = nthreads;
	BUG_ON(batch != 1);

	work_items = kzalloc(sizeof(struct copy_page_info)*num_work_item,
						 GFP_KERNEL);
	if (!work_items) {
		goto out_page;
	}

	pr_info("Zi: chunk_size: %lu\n", chunk_size);

#ifdef CONFIG_X86
	vfrom = kmap_atomic(start_page[0]);
	vto = kmap_atomic(end_page[0]);
#endif
#ifdef CONFIG_PPC64
	vfrom = kmap(start_page[0]);
	vto = kmap(end_page[0]);
#endif

	timestamp = rdtsc();
	if (nthreads == 1) {
#ifdef CONFIG_X86
		_memcpy(vto, vfrom, PAGE_SIZE<<page_order);
#endif
#ifdef CONFIG_PPC64
		memcpy(vto, vfrom, PAGE_SIZE<<page_order);
#endif
	} else {
		for (i = 0; i < nthreads; ++i) {
				INIT_WORK((struct work_struct *)&work_items[i], copy_page_thread);

				work_items[i].from = vfrom + chunk_size * i;
				work_items[i].to = vto + chunk_size * i;

				work_items[i].chunk_size = chunk_size;
				/* Queue the work on CPUs  */
				queue_work_on(cpu_id[i], system_highpri_wq, (struct work_struct*)&work_items[i]);
		}
		
		for (i = 0; i < nthreads; ++i) {
			flush_work((struct work_struct*)&work_items[i]);
		}
		/*flush_workqueue(system_highpri_wq);*/
	}

	duration = rdtsc() - timestamp;
#ifdef CONFIG_PPC64
	pr_info("Page order: %d copy done after %llu cycles, %llu microsec, %d threads\n", 
			page_order, duration, tb_to_ns(duration)/1000, nthreads);
#endif

#ifdef CONFIG_X86
	pr_info("Page order: %d copy done after %llu cycles, %llu microsec, %d threads\n", 
			page_order, duration, duration/2600, nthreads);
#endif

#ifdef CONFIG_X86
	kunmap_atomic(vto);
	kunmap_atomic(vfrom);
#endif

#ifdef CONFIG_PPC64
	kunmap(start_page[0]);
	kunmap(end_page[0]);
#endif

	for (i = 0; i < batch; ++i) {
#ifdef CONFIG_X86
		vpage = kmap_atomic(end_page[i]);
#endif
#ifdef CONFIG_PPC64
		vpage = kmap(end_page[i]);
#endif

		for (k = 0; k < PAGE_SIZE<<page_order; k += PAGE_SIZE) {
			if (vpage[k] != (char)(i+1)) {
				pr_err("page offset %lu at batch %d corrupted\n", k, i);
				break;
			}
			
		}
#ifdef CONFIG_X86
		kunmap_atomic(vpage);
#endif
#ifdef CONFIG_PPC64
		kunmap(end_page[i]);
#endif
	}

	kfree(work_items);

	return 0;

out_page:
	for (i = 0; i < batch; ++i) {
		if (end_page[i]) {
			__free_pages(end_page[i], page_order);
			end_page[i] = NULL;
		}
		if (start_page[i]) {
			__free_pages(start_page[i], page_order);
			start_page[i] = NULL;
		}
	}

out_free_page_list:
	if (end_page)
		kfree(end_page);
	end_page = NULL;
	if (start_page)
		kfree(start_page);
	start_page = NULL;

	if (pollution)
		free_pages_exact(pollution, pollution_size);
	pollution = NULL;


out_free_timestamp:
	kfree(begin_timestamps);
	begin_timestamps = NULL;
	return 0;
}

static void __exit bench_exit(void)
{
	int i;
	
	for (i = 0; i < batch; ++i) {
		if (end_page)
			if (end_page[i])
				__free_pages(end_page[i], page_order);
		if (start_page)
			if (start_page[i])
				__free_pages(start_page[i], page_order);
	}

	if (end_page)
		kfree(end_page);
	if (start_page)
		kfree(start_page);

	if (cpu_id)
		kfree(cpu_id);
	if (begin_timestamps)
		kfree(begin_timestamps);
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
