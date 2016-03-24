#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/highmem.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#ifndef CONFIG_X86
# error "This module only works on X86"
#endif

MODULE_LICENSE("GPL");

static int node = 1;
module_param(node, int, S_IRUGO);
/*static uint iterations = 1000;*/
/*module_param(iterations, uint, S_IRUGO);*/

/*static bool is_tlb_flush = 1;*/
/*module_param(is_tlb_flush, bool, S_IRUGO);*/


static void copy_pages(struct page *to, struct page *from)
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
		for (i = 0; i < 4096/32; i++) {
			__asm__ __volatile__ (
			" vmovntdqa (%0), %%ymm0\n"
			/*" vmovntdq  %%ymm0, (%1)\n"*/
			/*" vmovntdqa 32(%0), %%ymm0\n"*/
			/*" vmovntdq  %%ymm1, 32(%1)\n"*/
			/*" vmovntdqa 64(%0), %%ymm0\n"*/
			/*" vmovntdq  %%ymm2, 64(%1)\n"*/
			/*" vmovntdqa 96(%0), %%ymm0\n"*/
			/*" vmovntdq  %%ymm3, 96(%1)\n"*/
			/*" vmovntdqa 128(%0), %%ymm0\n"*/
			/*" vmovntdq  %%ymm4, 128(%1)\n"*/
			/*" vmovntdqa 160(%0), %%ymm0\n"*/
			/*" vmovntdq  %%ymm5, 160(%1)\n"*/
			/*" vmovntdqa 192(%0), %%ymm0\n"*/
			/*" vmovntdq  %%ymm6, 192(%1)\n"*/
			/*" vmovntdqa 224(%0), %%ymm0\n"*/
			/*" vmovntdq  %%ymm7, 224(%1)\n"*/
				: : "r" (vfrom), "r" (vto) : "memory");
			/*vfrom += 256;*/
			/*vto += 256;*/
			vfrom += 32;
			vto += 32;
			__asm__ __volatile__("lfence \n"::);
		}
		/*
		 * Since movntq is weakly-ordered, a "sfence" is needed to become
		 * ordered again:
		 */
		/*__asm__ __volatile__("sfence \n"::);*/
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
	struct page* start_page[8] = {0};
	struct page* end_page[8] = {0};
	int cpu;
	int i, j;
	ulong irqs;

	void *vpage;
	unsigned cycles_high, cycles_low;
	unsigned cycles_high1, cycles_low1;
	uint64_t start;
	uint64_t end;


	for (i = 0; i < 8; ++i) {
		start_page[i] = __alloc_pages_node(0, (GFP_HIGHUSER_MOVABLE |
									  __GFP_THISNODE), 10);

		if (!start_page[i]) {
			pr_err("fail to allocate start page in iteration: %d", i);
			goto out_page;
		}
		end_page[i] = __alloc_pages_node(0, (GFP_HIGHUSER_MOVABLE |
									  __GFP_THISNODE), 10);
		if (!end_page[i]) {
			pr_err("fail to allocate end page in iteration: %d", i);
			goto out_page;
		}
	}
	pr_info("start page: %d, end_page: %d", page_to_nid(start_page[0]), page_to_nid(end_page[0]));

	for (i = 0; i < 8; ++i) {
		vpage = kmap_atomic(start_page[i]);
		set_memory_wc((unsigned long)vpage, 1024);
		kunmap_atomic(vpage);
	}

	/* Disable interrupts */
	cpu = get_cpu();

	pr_info("get_cpu: %d\n", cpu);

	local_irq_save(irqs);

	asm volatile
		( "CPUID\n\t"
		  "RDTSC\n\t"
		  "mov %%edx, %0\n\t"
		  "mov %%eax, %1\n\t"
		  :
		  "=r" (cycles_high), "=r" (cycles_low)
		  ::
		  "rax", "rbx", "rcx", "rdx"
		);

	for (i = 0; i < 8; ++i) {
		for (j = 0; j < 1024; ++j)
			copy_pages_nocache(end_page[i]+j, start_page[i]+j);
	}

	asm volatile
		( "RDTSCP\n\t"
		  "mov %%edx, %0\n\t"
		  "mov %%eax, %1\n\t"
		  "CPUID\n\t"
		  :
		  "=r" (cycles_high1), "=r" (cycles_low1)
		  ::
		  "rax", "rbx", "rcx", "rdx"
		);
	start = ((uint64_t)cycles_high <<32 | cycles_low);
	end = ((uint64_t)cycles_high1 <<32 | cycles_low1);


	/* Print results */
	printk("Total %lu accesses use %llu cycles, on average %llu cycles per access",
			8UL*4*1024*1024/32, end-start, (end-start)/(8UL*4*1024*1024/32));

	local_irq_restore(irqs);

	for (i = 0; i < 8; ++i) {
		vpage = kmap_atomic(start_page[i]);
		set_memory_wb((unsigned long)vpage, 1024);
		kunmap_atomic(vpage);
	}
	/* Enable interrupts */
	put_cpu();

out_page:
	for (i = 0; i < 8; ++i) {
		if (end_page[i])
			__free_pages(end_page[i], 10);
		if (start_page[i])
			__free_pages(start_page[i], 10);
	}
	return 0;
}

static void __exit bench_exit(void)
{
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
