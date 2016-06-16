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

u64 *begin_timestamps = NULL;


int copy_page_thread(void *data)
{
	int i = *(int*)data;
	int j;
	unsigned int cpu_id = 0;
	const struct cpumask *cpumask = cpumask_of_node(node);
	struct task_struct *tsk = current;
	u64 current_timestamp;


	cpu_id = cpumask_first(cpumask);

	for (j = 0; j < i; j++)
		cpu_id = cpumask_next(cpu_id, cpumask);

	if (cpu_id < nr_cpu_ids)
		set_cpus_allowed_ptr(tsk, cpumask_of(cpu_id));
	else
		return 0;

	current_timestamp = rdtsc();
	pr_info("kthread: %d, used %llu cycles, %llu microsec to run",
			i, current_timestamp - begin_timestamps[i], 
			(current_timestamp - begin_timestamps[i])/2600);

	return 0;
}

static int __init bench_init(void)
{
	int i;

	memhog_threads = kmalloc_node(sizeof(struct task_struct)*nthreads, GFP_KERNEL, node);
	if (!memhog_threads)
		goto out;

	begin_timestamps = kmalloc_node(sizeof(u64)*nthreads, GFP_KERNEL, node);
	if (!begin_timestamps)
		goto out_free_task;


	for (i = 0; i < nthreads; ++i) {
		begin_timestamps[i] = rdtsc();
		memhog_threads[i] = kthread_create_on_node(copy_page_thread, &i, node,
							"memhog_kernel%d", i);
		if (!IS_ERR(memhog_threads[i]))
			wake_up_process(memhog_threads[i]);
		else
			pr_err("create memhog_threads%d failed", i);
	}

	return 0;


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
	
	if (begin_timestamps)
		kfree(begin_timestamps);
	if (memhog_threads)
		kfree(memhog_threads);
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
