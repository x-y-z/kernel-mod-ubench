#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/migrate.h>
#include <linux/kthread.h>

MODULE_LICENSE("GPL");

struct thread_data {
	volatile int *data;
};

static int keep_reading(void *data)
{
	struct thread_data *arg = (struct thread_data *)data;
	int tmp;

	while (true) {
		tmp += *(arg->data);
		cond_resched();
		if (kthread_should_stop())
			break;
	};

	return 0;
}

static int __init bench_init(void)
{
	char * src_addr = vmalloc(PAGE_SIZE);
	volatile int *data = (int *)src_addr;
	struct page *src_page;
	struct page *after_src_page;
	struct thread_data t_data;
	struct task_struct *my_kthread;

	memset(src_addr, 0, PAGE_SIZE);

	src_page = vmalloc_to_page(src_addr);

	pr_info("before: vaddr: %lx, src: %lx\n", ((unsigned long)src_addr)>>PAGE_SHIFT, page_to_pfn(src_page));

	*data = 0x4f;
	t_data.data = data;

	my_kthread = kthread_run(keep_reading, &t_data, "test_vmalloc_migration");
	pr_info("kthread launched\n");
	
	migrate_vmalloc_pages(src_addr);
	pr_info("migrate done\n");

	after_src_page = vmalloc_to_page(src_addr);
	pr_info("vmalloc_to_page done\n");

	pr_info("after: vaddr: %lx, src: %lx, value: %x\n", ((unsigned long)src_addr)>>PAGE_SHIFT, page_to_pfn(after_src_page), *data);

	kthread_stop(my_kthread);
	pr_info("kthread stopped");

	vfree(src_addr);
	pr_info("after free src_addr: page ref: src: %d, dst: %d\n", page_count(src_page), page_count(after_src_page));
	return 0;
}

static void __exit bench_exit(void)
{
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
