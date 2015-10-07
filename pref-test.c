#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#if 0
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/random.h>

#include <asm/cacheflush.h>
#endif
#include <asm/tlbflush.h>

#ifndef CONFIG_X86
# error "This module only works on X86"
#endif

MODULE_LICENSE("GPL");

static uint iterations = 10000;
module_param(iterations, uint, S_IRUGO);

static bool tlb_flush = 1;
module_param(tlb_flush, bool, S_IRUGO);

static uint core1 = 0;
module_param(core1, uint, S_IRUGO);

static uint core2 = 1;
module_param(core2, uint, S_IRUGO);

static struct perf_event_attr tlb_miss_event_attr = {
	.type           = PERF_TYPE_HW_CACHE,
	.config		= PERF_COUNT_HW_CACHE_DTLB | PERF_COUNT_HW_CACHE_OP_READ << 8 | PERF_COUNT_HW_CACHE_RESULT_MISS << 16,
/*	.config         = 0x0108, /* perf_event_intel.c:610,
	                             SNB_DTLB_READ_MISS_TO_PTW: 0x0108
				     SNB_ITLB_READ_MISS_TO_PTW: 0x0185
				   */
	.size           = sizeof(struct perf_event_attr),
	.pinned         = 1,
	.disabled       = 1,
};


struct test {
	char * data;
	struct kref refcount;
	atomic_t accesses;
	atomic_t invalidations;
};

static void release_test(struct kref *ref)
{
	struct test *t = container_of(ref, struct test, refcount);
	pr_info("Releasing test struct\n");
	vfree(t->data);
	kfree(t);
}

static int test_invalidator(void* data)
{
	struct test *t = data;
	int cpu = get_cpu();
	int i;

	pr_info("Test invalidator on cpu %d\n", cpu);

	for (i = 0; i < iterations; ++i) {
		while (atomic_xchg(&t->accesses, 0) == 0);
		if (tlb_flush) {
			__flush_tlb_single((uintptr_t)t->data);
//			pr_info("Consumed access, produced invalidation: %u\n", i);
		}
		atomic_set(&t->invalidations, 1);
	}
	pr_info("Invalidator done\n");
	put_cpu();
	kref_put(&t->refcount, release_test);
	return 1;
}

static int test_accessor(void* data)
{
	struct test *t = data;
	volatile char * d = t->data;
	struct perf_event *tlb_miss;
	int cpu = get_cpu();
	unsigned i;
	int ret = 0;
	u64 tlb_misses_begin, tlb_misses_end, running, enabled;

	pr_info("Test accessor on cpu %d\n", cpu);

	/* Setup TLB miss counter */
	tlb_miss = perf_event_create_kernel_counter(&tlb_miss_event_attr,
		cpu, NULL, NULL, NULL);
	if (IS_ERR(tlb_miss)) {
		pr_err("Failed to create kernel counter\n");
		goto out_putcpu;
	}
	perf_event_enable(tlb_miss);

//	local_irq_save(irqs);
	/* Read TLB miss and Cache miss counters */
	tlb_misses_begin = perf_event_read_value(tlb_miss, &enabled, &running);
	for (i = 0; i < iterations; ++i) {
		while (atomic_xchg(&t->invalidations, 0) == 0);
//		pr_info("Consumed invalidation, produced access: %u\n", i);
		ret = (ret << 1) ^d[0];
		atomic_set(&t->accesses, 1);
	}

	tlb_misses_end = perf_event_read_value(tlb_miss, &enabled, &running);


//	local_irq_restore(irqs);

	/* Print results */
	pr_info("Iterations: %u\n", iterations);
	pr_info("Result: %u\n", ret);
	pr_info("TLB misses: %llu (%llu - %llu)\n",
		tlb_misses_end - tlb_misses_begin,
		tlb_misses_end, tlb_misses_begin);

	/* Clean up counters */
	perf_event_disable(tlb_miss);

	perf_event_release_kernel(tlb_miss);
out_putcpu:
	pr_info("Accessor done\n");
	put_cpu();
	kref_put(&t->refcount, release_test);
	return 0;
}

static int __init bench_init(void)
{
	struct task_struct *t1, *t2;
	struct test * t;

	pr_info("Hello World!\n");
	if (core1 == core2) {
		pr_err("Running both threads on the same core will hang your "
		       "machine.\n");
		return -EINVAL;
	}
	t = kzalloc(sizeof(struct test), GFP_KERNEL);
	if (!t)
		goto out;

	t->data = vmalloc(PAGE_SIZE);
	if (!t->data)
		goto out_free;

	/* add reference for both threads */
	kref_init(&t->refcount);
	kref_get(&t->refcount);

	/* init semaphores */
	t->data[0] = 0x5;
	atomic_set(&t->accesses, 1);
	atomic_set(&t->invalidations, 0);


	t1 = kthread_create(test_invalidator, t, "test_invalidator");
	if (IS_ERR(t1))
		goto out_vfree;

	t2 = kthread_create(test_accessor, t, "test_accessor");
	if (IS_ERR(t2))
		goto out_stop1;

	kthread_bind(t1, core1);
	kthread_bind(t2, core2);
	wake_up_process(t1);
	wake_up_process(t2);

	pr_info("init done\n");
	return 0;

	kthread_stop(t2);
out_stop1:
	kthread_stop(t1);
out_vfree:
	vfree(t->data);
out_free:
	kfree(t);
out:
	pr_warning("Something went wrong\n");
	return 1;
}

static void __exit bench_exit(void)
{
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
