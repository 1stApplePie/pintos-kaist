/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#define LIMIT_STACK_SIZE 1 << 20

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, page_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, page_elem);
  const struct page *b = hash_entry (b_, struct page, page_elem);

  return a->va < b->va;
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* Create the page, fetch the initialier according to the VM type,
		 * and then create "uninit" page struct by calling uninit_new. You
		 * should modify the field after calling the uninit_new. */
		
		struct page *new_page = (struct page *)malloc(sizeof(struct page));
		if (new_page == NULL)
            goto err;		

		void *va = pg_round_down(upage);

		switch (VM_TYPE(type)) {
            case VM_ANON:
                uninit_new(new_page, va, init, type, aux, anon_initializer);
                break;
            case VM_FILE:
                uninit_new(new_page, va, init, type, aux, file_backed_initializer);
                break;
            default:
                NOT_REACHED();
                break;
        }

		new_page->writable = writable;

		/* Insert the page into the spt. */
		if (!spt_insert_page(spt, new_page)) {
			goto err;
		}
	}
	return true;
err:
	return false;
}

/* Returns the page containing the given virtual address, or a null pointer if no such page exists. */
/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page *page = (struct page *)malloc(sizeof(struct page));
	/* Find page in supplement page table - implemented project 3 */
	struct hash_elem *e;

	page->va =  pg_round_down(va);
	e = hash_find (&spt->spt_ht, &page->page_elem);

	free(page);
	return e != NULL ? hash_entry (e, struct page, page_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {
	/* Insert page in supplement page - implemented project 3 */
	struct hash_elem *e = hash_insert (&spt->spt_ht, &page->page_elem);

	return e == NULL ? true : false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	
	/* TODO: Fill this function. */
	if (frame == NULL) {
		printf("frame malloc failed!\n");
		exit(-1);
	}

	void *kva = palloc_get_page(PAL_USER);
	if (kva == NULL) {
		free(frame);
		kva = vm_evict_frame();
	}

	frame->kva = kva;
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	void *stack_bottom = pg_round_down(addr);
	bool succ = false;

	/* Check if stack size exceeds 1MB */
	if (pg_ofs(addr) + PGSIZE > LIMIT_STACK_SIZE) {
		printf("Stack size exceeds 1MB!\n");
		exit(-1);
	}

	thread_current()->stack_bottom = stack_bottom;

	while (vm_alloc_page(VM_ANON, stack_bottom, true)) {
		vm_claim_page(stack_bottom);
		stack_bottom += PGSIZE;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr, bool user, bool write, bool not_present) {
	struct thread *curr = thread_current();
	struct supplemental_page_table *spt = &curr->spt;
	struct page *page = spt_find_page(spt, addr);
	// Valid check
	if (user == true && !is_user_vaddr(addr)) {
		return false;
	}
	else if (!not_present) {
		return false;
	}
	/*  three cases of bogus page fault: 
		lazy-loaded, swaped-out page, and write-protected page */

	/* case 1: lazy loaded  - default*/

	if (page == NULL) {
		// stack bottom > addr > LIMIT_STACK_SIZE && user
		if (pg_ofs(addr) + PGSIZE < LIMIT_STACK_SIZE && addr < curr->user_rsp && user) {
			vm_stack_growth(addr);
			return true;
		}
		return false;
	}

	// process_exec, load에서 addr에 페이지가 존재하는 이유?
	// uninit page를 초기화 할 때 spt에 넣어줌. 이는 lazy load떄문에 frame에 할당되어있지 않음
	// 이제 do_claim_page의 swap_in에서 anon_initialize가 실행되야 함
	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	/* TODO: Fill this function */
	struct page *page = spt_find_page(&thread_current()->spt, va);
	return page != NULL ? vm_do_claim_page(page) : false;
}

/* Claim the PAGE and set up the mmu. */
// 1. lazy load 방식에서 실제 페이지가 처음으로 사용될 때 va에 frame을 할당하는 함수
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	if (frame == NULL)
		return false;
		
	struct thread *curr = thread_current();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* Insert page table entry to map page's VA to frame's PA - implemented project 3 */
	pml4_set_page(curr->pml4, page->va, frame->kva, page->writable);

	// 프로세스가 특정 가상 주소에 접근하려고 시도하면 페이지 폴트가 발생합니다.
	// 페이지 폴트 핸들러가 호출되고, 해당 페이지에 대한 정보를 찾아야 합니다.
	// 만약 해당 페이지가 물리 메모리에 없고 스왑 영역에 저장되어 있다면, swap_in 함수가 호출되어 페이지를 물리 메모리로 가져옵니다.
	// 가져온 페이지의 정보를 페이지 테이블에 업데이트하고, 프로세스는 해당 가상 주소에 접근할 수 있게 됩니다.


	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table 
Supplements the page table with additional information about each page.

Why not just change page table directly? Limitations on page table format.

Two purposes:
1. On page fault, kernel looks up virtual page in supplemental page table to
find what data should be there.
2. When a process terminates, kernel determines what resources to free.
*/
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init(&spt->spt_ht, page_hash, page_less, NULL);
}

/* Copies the supplemental page table from src to dst. 
This is used when a child needs to inherit the execution context of
its parent (i.e. fork()). Iterate through each page 
in the src's supplemental page table and make a exact copy of the entry
in the dst's supplemental page table. 

You will need to allocate uninit page and claim them immediately.
*/
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	
	// Iterate through each page in the source's supplemental page table
	struct hash_iterator i;
	hash_first(&i, &src->spt_ht);

	while (hash_next(&i)) {
		struct page *src_page = hash_entry(hash_cur(&i), struct page, page_elem);

        // Create a new page for the destination supplemental page table
		switch(VM_TYPE(src_page->operations->type)) {
			case VM_UNINIT :{
				// allocate uninit page and claim them
				vm_alloc_page_with_initializer(src_page->uninit.type, src_page->va,
					src_page->writable, src_page->uninit.init, src_page->uninit.aux);
				break;
			}
			case VM_ANON :{
				// uninit이 아니면 claim할 필요가 없으므로 operation의 type으로 페이지 생성
				vm_alloc_page(src_page->operations->type, src_page->va, src_page->writable);

				// vm_alloc_page에서 할당받은 페이지는 pa정보가 없다
				struct page *dst_page = spt_find_page(dst, src_page->va);
				if (dst_page == NULL) {
					return false;
				}

				// do_claim으로 dst_page에 물리 메모리를 할당
				if (!vm_do_claim_page(dst_page)) {
					return false;
				}

				// 처음 memcpy인자로 sizeof(void *)를 던졌는데,
				// 여기서 사이즈는 물리 메모리의 사이즈를 의미
				memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
				break;
			}
			case VM_FILE :{
				break;
			}
			default :
				break;
		}
	}
	return true;
}

static void spt_destroy(struct hash_elem *e, void *aux UNUSED) {
    const struct page *page = hash_entry(e, struct page, page_elem);
	/* Write back modified contents to storage if necessary */
	// if (page->operations->type == VM_FILE && page->operations->writeback) {
	//     page->operations->writeback(page);
	// }
    vm_dealloc_page(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	/*
	hash destroy 사용 시 실패, hash clear 사용 시 성공
	1. hash_destroy: 해시 테이블을 완전히 파괴하는 함수
					이 함수를 호출하면 해시 테이블에 있는 모든 원소들이 제거되고, 
					각 원소에 할당된 자원 해제 함수를 호출하여 자원을 해제합니다. 
					그리고 해시 테이블 자체도 메모리에서 해제됩니다. 
					이 함수는 해시 테이블을 초기 상태로 되돌리는 효과가 있습니다.
	2. hash_clear: hash_clear 함수는 해시 테이블의 내용을 제거하지만, 
					해시 테이블 자체는 남아있게 됩니다. 
					각 원소에 할당된 자원 해제 함수를 호출하여 자원을 해제하고, 
					원소들을 제거하여 해시 테이블을 비웁니다. 
					하지만 해시 테이블 객체 자체는 남아있습니다. 
					이 함수를 사용하면 해시 테이블은 여전히 사용 가능한 상태가 되며, 
					나중에 다시 사용할 수 있습니다.

	결국 우리가 원하는 작동은 process cleanup 후에 load하는 과정이므로 spt자체가 파괴되면 안된다.
	*/
	// hash_destroy(&spt->spt_ht, spt_destroy);
	hash_clear(&spt->spt_ht, spt_destroy);

}
