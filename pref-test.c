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

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/fpu/api.h>
#include <asm/set_memory.h>

#ifndef CONFIG_X86
# error "This module only works on X86"
#endif

MODULE_LICENSE("GPL");

/*#undef __va_wc*/
static int node = 0;
module_param(node, int, S_IRUGO);

static uint nthreads = 4;
module_param(nthreads, uint, S_IRUGO);

/*static bool is_tlb_flush = 1;*/
/*module_param(is_tlb_flush, bool, S_IRUGO);*/

static struct task_struct **memhog_threads = NULL;
static struct page** start_page = NULL;
static struct page** end_page = NULL;

static int thread_id[32] = {0};

static inline void copy_pages_nocache(struct page *to, struct page *from, unsigned long size)
{
	char *vfrom, *vto;
	char *vvfrom, *vvto;
	unsigned long i;

#ifdef __va_wc 
	vvfrom = vfrom = kmap_wc(from);
	vvto = vto = kmap_wc(to);
#else
	vvfrom = vfrom = kmap(from);
	vvto = vto = kmap(to);
#endif

	/*pr_info("vfrom: %p, pfn: 0x%lx, vto: %p, pfn: 0x%lx\n", vfrom, page_to_pfn(from), vto, page_to_pfn(to));*/

	if (boot_cpu_has(X86_FEATURE_AVX2)) {
		kernel_fpu_begin();
		for (i = 0; i < size/256; i++) {
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
			/*vfrom += 32;*/
			/*vto += 32;*/
		}
		/*
		 * Since movntq is weakly-ordered, a "sfence" is needed to become
		 * ordered again:
		 */
		__asm__ __volatile__("sfence \n"::);
		kernel_fpu_end();
	} else if (boot_cpu_has(X86_FEATURE_AVX)) {
		kernel_fpu_begin();
		for (i = 0; i < size/128; i++) {
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
	} else 
	{
		copy_page(vto, vfrom);
	}
#ifdef __va_wc
	kunmap_wc(to);
	kunmap_wc(from);
#else
	kunmap(to);
	kunmap(from);
#endif
}

int copy_page_thread(void *data)
{
	int i = *(int*)data;
	int j;
	unsigned int cpu_id = 0;
	const struct cpumask *cpumask = cpumask_of_node(node);
	struct task_struct *tsk = current;

	cpu_id = cpumask_first(cpumask);

	for (j = 0; j < i; j++)
		cpu_id = cpumask_next(cpu_id, cpumask);

	if (cpu_id < nr_cpu_ids)
		set_cpus_allowed_ptr(tsk, cpumask_of(cpu_id));
	else
		return 0;

	/*if (!cpumask_empty(cpumask))*/
		/*set_cpus_allowed_ptr(tsk, cpumask);*/

	while (!kthread_should_stop()) {
		copy_pages_nocache(end_page[i], start_page[i], PAGE_SIZE*1024);
		/* make watchdog happy  */
		cond_resched();
	}

	return 0;
}

static int __init bench_init(void)
{
	/*volatile char * from;*/
	/*volatile char * to;*/
	int i;
	/*ulong irqs;*/

#ifndef __va_wc
	void *vpage;
#endif

	if (nthreads > 16 || nthreads < 0)
		return 0;

	memhog_threads = kmalloc_node(sizeof(struct task_struct)*nthreads, 
							GFP_KERNEL, node);
	if (!memhog_threads)
		goto out;
	memset(memhog_threads, 0, sizeof(struct task_struct)*nthreads);

	start_page = kmalloc_node(sizeof(struct page)*nthreads, GFP_KERNEL, node);
	if (!start_page)
		goto out_free_task;
	end_page = kmalloc_node(sizeof(struct page)*nthreads, GFP_KERNEL, node);
	if (!end_page)
		goto out_free_start;


	for (i = 0; i < nthreads; ++i) {
		start_page[i] = __alloc_pages_node(node, (GFP_KERNEL|
									  __GFP_THISNODE) & ~__GFP_RECLAIM, 10);

		if (!start_page[i]) {
			pr_err("fail to allocate start page in iteration: %d", i);
			goto out_page;
		}
		end_page[i] = __alloc_pages_node(node, (GFP_KERNEL|
									  __GFP_THISNODE) & ~__GFP_RECLAIM, 10);
		if (!end_page[i]) {
			pr_err("fail to allocate end page in iteration: %d", i);
			goto out_page;
		}
	}

#ifndef __va_wc
	for (i = 0; i < nthreads; ++i) {
		vpage = kmap(start_page[i]);
		set_memory_wc((unsigned long)vpage, 1024);
		kunmap(vpage);
	}
	pr_info("Using set_memory_wc\n");
#endif

	/*pr_info("__va: %p, __va_wc: %p", __va(0), __va_wc(0));*/

	for (i = 0; i < nthreads; ++i) {
		thread_id[i] = i;
		memhog_threads[i] = kthread_create_on_node(copy_page_thread, 
							&thread_id[i], node,
							"memhog_kernel%d", i);
		if (!IS_ERR(memhog_threads[i]))
			wake_up_process(memhog_threads[i]);
		else
			pr_err("create memhog_threads%d failed", i);
	}

	return 0;


out_page:
	for (i = 0; i < nthreads; ++i) {
		if (end_page[i])
			__free_pages(end_page[i], 10);
		if (start_page[i])
			__free_pages(start_page[i], 10);
	}

	kfree(end_page);
out_free_start:
	kfree(start_page);

out_free_task:
	kfree(memhog_threads);
out:
	return 0;
}

static void __exit bench_exit(void)
{
	int i;
#ifndef __va_wc
	void *vpage;

	for (i = 0; i < nthreads; ++i) {
		vpage = kmap(start_page[i]);
		set_memory_wb((unsigned long)vpage, 1024);
		kunmap(vpage);
	}
#endif

	for (i = 0; i < nthreads; ++i) {
		if (memhog_threads[i])
			kthread_stop(memhog_threads[i]);
	}



	for (i = 0; i < nthreads; ++i) {
		if (end_page[i])
			__free_pages(end_page[i], 10);
		if (start_page[i])
			__free_pages(start_page[i], 10);
	}
	
	if (end_page)
		kfree(end_page);
	if (start_page)
		kfree(start_page);
	if (memhog_threads)
		kfree(memhog_threads);
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
