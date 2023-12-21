/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	// swap_disk = NULL;
}

/* Initialize the file mapping */
// Anonymous 페이지의 경우에는 실제 파일이나 디스크에 매핑되는 것이 아니라, 
// 메모리 내에서만 사용되는 페이지
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->thread = thread_current();

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if (page->frame != NULL) {
		// frame의 kva를 할당 해제하면 정보를 담고있는 pt가 미아가 된다?
		// process_cleanup에서 spt해제 후에 pml4를 해제하면서 에러가 발생
		// pte에 대한 destory는 pml4가 담당

		// if (page->frame->kva != NULL) {
		// 	palloc_free_page(page->frame->kva);
		// }
		free(page->frame);
	}
}
