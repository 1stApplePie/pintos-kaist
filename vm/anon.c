/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/vm.h"
#include "devices/disk.h"

#include <bitmap.h>

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

struct bitmap *swap_bitmap;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);

	if (swap_disk != NULL) {
		// disk size는 sector 수를 반환, 각 sector는 512 byte sector임
		// 우리는 4KB page를 할당해야 하므로, length = disk_size / 8 의 비트맵을 만들면 됨
		swap_bitmap = bitmap_create (disk_size(swap_disk) / 8);
	}
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
	size_t bitmap_idx = page->bitmap_idx;

	for (int i = 0; i < 8; i++) {
        disk_read(swap_disk, bitmap_idx*8+i, page->frame->kva + (i * DISK_SECTOR_SIZE));
    }
	bitmap_set(swap_bitmap, bitmap_idx, false);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	size_t bitmap_idx = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
	if (bitmap_idx == BITMAP_ERROR)
        return false;

	page->bitmap_idx = bitmap_idx;
	for (int i = 0; i < 8; i++) {
        disk_write(swap_disk, bitmap_idx*8+i, page->frame->kva + (i * DISK_SECTOR_SIZE));
    }
	memset(page->frame->kva, 0, PGSIZE);

	// page table update
	pml4_clear_page(thread_current()->pml4, page->va);
	pml4_set_dirty(anon_page->thread->pml4, page->va, false);
    page->frame = NULL;
	
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if (page->frame != NULL) {
		free(page->frame);
	}
}
