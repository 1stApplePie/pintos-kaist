/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	if (page->frame != NULL) {
		free(page->frame);
	}
	// file_close(file_page->file);
}

static bool
mmap_lazy_load (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct mmap_info *mmap_info = (struct mmap_info *)aux;
	struct file_page *file_page = &page->file;
	list_push_back(&(thread_current()->mmap_info_list), &(file_page->file_elem));
	
	off_t res = file_read_at (mmap_info->file, page->va, 
					mmap_info->read_bytes, mmap_info->ofs);

	if (res != mmap_info->read_bytes) {
		return false;
	}

	memset((char *)page->va + mmap_info->read_bytes, 0, PGSIZE-mmap_info->read_bytes);

	file_page->page = page;
	file_page->file = mmap_info->file;
	file_page->ofs = mmap_info->ofs;
	file_page->read_bytes = mmap_info->read_bytes;
	file_page->length = mmap_info->length;

	pml4_set_dirty(thread_current()->pml4, page->va, false);
	free(aux);

	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	// length bytes the file open as fd starting from offset byte 
	// into the process's virtual address space at addr

	// You should use the file_reopen function to obtain a separate and 
	// independent reference to the file for each of its mappings
	struct file *reopen_file = file_reopen(file);
	if (reopen_file == NULL) {
        return NULL;
    }

	// Memory-mapped pages should be also allocated in a lazy manner 
	// just like anonymous pages. 
	// You can use vm_alloc_page_with_initializer or 
	// vm_alloc_page to make a page object.
	size_t read_bytes = file_length(reopen_file);
	size_t zero_bytes = PGSIZE - (read_bytes % PGSIZE);
	void *upage = addr;

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct mmap_info *aux  = (struct mmap_info *)malloc(sizeof(struct mmap_info));
		if (aux == NULL) {
			return false;
		}

		aux->file = reopen_file;
		aux->ofs = offset;
		aux->read_bytes = page_read_bytes;
		aux->length = length;

		if (!vm_alloc_page_with_initializer (VM_FILE, upage,
					writable, mmap_lazy_load, aux)) {
						file_close(reopen_file);
						free(aux);
						return false;
					}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		offset += PGSIZE;
	}
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	// all pages written to by the process are written back to the file
	// pages not written must not be
	// The pages are then removed from the process's list of virtual pages.
	struct thread *curr = thread_current();
	struct page *page = spt_find_page(&curr->spt, addr);
	struct file_page *file_page = &page->file;
	void *upage = addr;

	uint32_t write_bytes = 0;
	uint32_t length = file_page->length;

	while (write_bytes < length) {
        struct page *page = spt_find_page(&curr->spt, upage);
		struct file_page *file_page = &page->file;

        if (pml4_is_dirty(curr->pml4, upage)) {
            file_seek(file_page->file, file_page->ofs);
            file_write(file_page->file, upage, file_page->read_bytes);
		}
		else {
			break;
		}

        hash_delete(&(curr->spt), &(page->page_elem));
        spt_remove_page(&curr->spt, page);
        vm_dealloc_page(page);

        upage += PGSIZE;
		write_bytes += PGSIZE;
    }
	file_close(file_page->file);
}
