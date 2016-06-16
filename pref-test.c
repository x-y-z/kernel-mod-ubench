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

#ifndef CONFIG_X86
# error "This module only works on X86"
#endif

MODULE_LICENSE("GPL");

static int node = 0;
module_param(node, int, S_IRUGO);

static uint nthreads = 4;
module_param(nthreads, uint, S_IRUGO);

/*static bool is_tlb_flush = 1;*/
/*module_param(is_tlb_flush, bool, S_IRUGO);*/

static struct task_struct **memhog_threads = NULL;

static u64 *begin_timestamps = NULL;
static unsigned int *cpu_id = NULL;

static int thread_id[32] = {0};

int copy_page_thread(void *data)
{
	int i = *(int*)data;
	u64 current_timestamp;



	current_timestamp = rdtsc();
	pr_info("kthread: %d, used %llu cycles, %llu microsec to run",
			i, current_timestamp - begin_timestamps[i], 
			(current_timestamp - begin_timestamps[i])/2600);

	return 0;
}

static int __init bench_init(void)
{
	int i;
	const struct cpumask *cpumask = cpumask_of_node(node);


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

	for (i = 0; i < nthreads; ++i) {
		begin_timestamps[i] = rdtsc();
		memhog_threads[i] = kthread_create_on_node(copy_page_thread, &thread_id[i], node,
							"memhog_kernel%d", i);
		kthread_bind(memhog_threads[i], cpu_id[i]);
		if (!IS_ERR(memhog_threads[i])) {
			wake_up_process(memhog_threads[i]);
		}
		else
			pr_err("create memhog_threads%d failed", i);
	}
	/*for (i = 0; i < nthreads; ++i) {*/
			/*wake_up_process(memhog_threads[i]);*/
	/*}*/

	return 0;

out_free_timestamp:
	kfree(begin_timestamps);
out_free_task:
	kfree(memhog_threads);
out:
	return 0;
}

static void __exit bench_exit(void)
{
	int i;
	for (i = 0; i < nthreads; ++i) {
		kthread_stop(memhog_threads[i]);
	}
	
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
