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
	struct file_page *file_page UNUSED = &page->file;
}

static bool
mmap_lazy_load (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct file_info *file_info = (struct file_info *)aux;
	
	off_t res = file_read_at (file_info->file, page->va, 
					file_info->read_bytes, file_info->ofs);
	memset((char *)page->va + file_info->read_bytes, 0, PGSIZE-file_info->read_bytes);

	page->file.file = file_info->file;

	if (res != file_info->read_bytes) {
		return false;
	}

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


	// Memory-mapped pages should be also allocated in a lazy manner 
	// just like anonymous pages. 
	// You can use vm_alloc_page_with_initializer or 
	// vm_alloc_page to make a page object.
	size_t read_bytes = length;
	size_t zero_bytes = PGSIZE - length;

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct file_info *aux  = (struct file_info *)malloc(sizeof(struct file_info));
		if (aux == NULL) {
			return false;
		}

		aux->file = reopen_file;
		aux->ofs = offset;
		aux->read_bytes = page_read_bytes;
		aux->writable = writable;

		if (!vm_alloc_page_with_initializer (VM_FILE, addr,
					writable, mmap_lazy_load, aux)) {
						file_close(reopen_file);
						free(aux);
						return false;
					}

		/* Advance. */
		struct thread *t = thread_current();
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += PGSIZE;
	}
	return addr;

	file_read_at(file, addr, length, offset);
}

/* Do the munmap */
void
do_munmap (void *addr) {
	// all pages written to by the process are written back to the file
	// pages not written must not be
	// The pages are then removed from the process's list of virtual pages.
	struct thread *curr = thread_current();
	struct page *page = spt_find_page(&curr->spt, addr);
	struct file *file = page->file.file;

	file_close(file);
}
