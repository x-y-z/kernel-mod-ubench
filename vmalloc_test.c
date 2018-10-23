#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/migrate.h>


static int __init bench_init(void)
{
	char * src_addr = vmalloc(PAGE_SIZE);
	int *data = (int *)src_addr;
	struct page *src_page, *dst_page;
	struct page *after_src_page;

	memset(src_addr, 0, PAGE_SIZE);

	src_page = vmalloc_to_page(src_addr);
	dst_page = alloc_page(GFP_KERNEL);

	pr_info("page ref: src: %d, dst: %d\n", page_count(src_page), page_count(dst_page));

	pr_info("before: vaddr: %lx, src: %lx, dst: %lx\n", ((unsigned long)src_addr)>>PAGE_SHIFT, page_to_pfn(src_page), page_to_pfn(dst_page));

	*data = 0x4f;
	
	migrate_vmalloc_pages(src_addr, dst_page);

	after_src_page = vmalloc_to_page(src_addr);

	pr_info("after: vaddr: %lx, src: %lx, value: %x\n", ((unsigned long)src_addr)>>PAGE_SHIFT, page_to_pfn(after_src_page), *data);

	vfree(src_addr);
	pr_info("after free src_addr: page ref: src: %d, dst: %d\n", page_count(src_page), page_count(dst_page));
	__free_page(src_page);
	pr_info("after free both: page ref: src: %d, dst: %d\n", page_count(src_page), page_count(dst_page));
	return 0;
}

static void __exit bench_exit(void)
{
	pr_info("Goodbye, world\n");
}

module_init(bench_init);
module_exit(bench_exit);
