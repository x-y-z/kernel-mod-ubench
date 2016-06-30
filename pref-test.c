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

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/fpu/api.h>

#ifndef CONFIG_X86
# error "This module only works on X86"
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

static struct task_struct **memhog_threads = NULL;

static u64 *begin_timestamps = NULL;
static unsigned int *cpu_id = NULL;

static int thread_id[32] = {0};

static atomic_t finished_threads;
static DECLARE_WAIT_QUEUE_HEAD(wait_for_other_threads);

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

int copy_page_thread(void *data)
{
	unsigned long i = *(int*)data;
	int batch_idx;
	u64 current_timestamp, tmp2;
	char *vfrom, *vto;
	unsigned long chunk_size = (PAGE_SIZE<<page_order)/nthreads;
	unsigned long j;



	current_timestamp = rdtsc();
	/*pr_info("kthread: %lu at %d, used %llu cycles, %llu microsec to run",*/
			/*i, smp_processor_id(),*/
			/*current_timestamp - begin_timestamps[i], */
			/*(current_timestamp - begin_timestamps[i])/2600);*/


	/*vfrom = kmap_atomic(start_page[i]);*/
	/*vto = kmap_atomic(end_page[i]);*/

	/*memcpy(vto, vfrom, PAGE_SIZE<<page_order);*/

	/*kunmap_atomic(vto);*/
	/*kunmap_atomic(vfrom);*/

	/*if (chunk_size > PAGE_SIZE) {*/
		/*for (batch_idx = 0; batch_idx < batch; ++batch_idx) {*/
			/*vfrom = kmap_atomic(start_page[batch_idx]);*/
			/*vto = kmap_atomic(end_page[batch_idx]);*/

			/*for (j = 0; j < chunk_size; j += PAGE_SIZE)*/
				/*copy_pages_nocache(&vto[j], &vfrom[j]);*/
			

			/*kunmap_atomic(vto);*/
			/*kunmap_atomic(vfrom);*/
		/*}*/
	/*} else {*/
		for (batch_idx = 0; batch_idx < batch; ++batch_idx) {
			vfrom = kmap_atomic(start_page[batch_idx]);
			vto = kmap_atomic(end_page[batch_idx]);

			
			_memcpy(&vto[i*chunk_size], &vfrom[i*chunk_size], chunk_size);

			kunmap_atomic(vto);
			kunmap_atomic(vfrom);
		}
	/*}*/

	tmp2 = rdtsc() - current_timestamp;
	pr_info("kthread: %lu at %d, used %llu cycles, %llu microsec to finish %lu KB",
			i, smp_processor_id(),
			tmp2, 
			tmp2/2600,
			chunk_size/1024);

	atomic_inc(&finished_threads);
	wake_up(&wait_for_other_threads);

	return 0;
}

static int __init bench_init(void)
{
	int i;
	u64 timestamp;
	u64 duration;
	const struct cpumask *cpumask = cpumask_of_node(node);
	char *vpage;
	unsigned long k;

	/*batch = nthreads;*/

	memhog_threads = kmalloc_node(sizeof(struct task_struct)*nthreads, GFP_KERNEL, node);
	if (!memhog_threads)
		goto out;

	begin_timestamps = kmalloc_node(sizeof(u64)*nthreads, GFP_KERNEL, node);
	if (!begin_timestamps)
		goto out_free_task;

	cpu_id = kmalloc_node(sizeof(unsigned int)*nthreads, GFP_KERNEL, node);

	cpu_id[0] = cpumask_first(cpumask);

	for (i = 1; i < nthreads; i++) {
		thread_id[i] = i;
		cpu_id[i] = cpumask_next(cpu_id[i-1], cpumask);

		if (cpu_id[i] > nr_cpu_ids)
			goto out_free_timestamp;
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

	pr_info("start page: %d, end_page: %d, cpus: %d", page_to_nid(start_page[0]), page_to_nid(end_page[0]), node);

	for (i = 0; i < batch; ++i) {
		vpage = kmap_atomic(start_page[i]);
		
		memset(vpage, 0, PAGE_SIZE<<page_order);

		for (k = 0; k < PAGE_SIZE<<page_order; k += PAGE_SIZE)
			vpage[k] = i;

		kunmap_atomic(vpage);
		vpage = kmap_atomic(end_page[i]);
		memset(vpage, 0, PAGE_SIZE<<page_order);
		kunmap_atomic(vpage);
	}

	atomic_set(&finished_threads, 0);
	for (i = 1; i < nthreads; ++i) {
		memhog_threads[i] = kthread_create_on_node(copy_page_thread, &thread_id[i], node,
							"memhog_kernel%d", i);
		kthread_bind(memhog_threads[i], cpu_id[i]);
	}

	begin_timestamps[0] = timestamp = rdtsc();
	for (i = 1; i < nthreads; ++i) {
		if (!IS_ERR(memhog_threads[i])) {

			begin_timestamps[i] = rdtsc();
			wake_up_process(memhog_threads[i]);
		}
		else
			pr_err("create memhog_threads%d failed", i);
	}
	copy_page_thread(&thread_id[0]);
	
	wait_event(wait_for_other_threads, 
			   atomic_read(&finished_threads) == nthreads);

	duration = rdtsc() - timestamp;
	pr_info("Page order: %d copy done after %llu cycles, %llu microsec, %d threads", 
			page_order, duration, duration/2600, nthreads);

	for (i = 0; i < batch; ++i) {
		vpage = kmap_atomic(end_page[i]);

		for (k = 0; k < PAGE_SIZE<<page_order; k += PAGE_SIZE) {
			if (vpage[k] != i)
				pr_err("page offset %lu at batch %d corrupted", k, i);
		}

		kunmap_atomic(vpage);
	}

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


out_free_timestamp:
	kfree(begin_timestamps);
	begin_timestamps = NULL;
out_free_task:
	kfree(memhog_threads);
	memhog_threads = NULL;
out:
	return 0;
}

static void __exit bench_exit(void)
{
	int i;
	/*for (i = 0; i < nthreads; ++i) {*/
		/*if (memhog_threads[i])*/
			/*kthread_stop(memhog_threads[i]);*/
	/*}*/
	
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
	if (memhog_threads)
		kfree(memhog_threads);
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
