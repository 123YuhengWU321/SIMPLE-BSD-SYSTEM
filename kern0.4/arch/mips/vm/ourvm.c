#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

#define OURVM_STACKPAGES    18     // this is taken from kern/arch/mips/vm/dumbvm.c
#define PAGE_SIZE           4096   // same as userland/lib/libc/stdlib/malloc.c:#define PAGE_SIZE 4096
#define DUMBVM_STACKPAGES   18	   // this is taken from kern/arch/mips/vm/dumbvm.c
/**
 * invalid is for illegal entries
 * true is if the entry is allocated
 * false is entry unallocated yet (free)
 */
#define invalid				-1
#define true				1
#define false				0

/*Macro for total nubmber of pages and coremap intilization checker*/
volatile int num_pages = 0;
volatile int coremap_initialized = false;	

/*
	design of our coremap_entry is adpated from the follwing link:
	http://jhshi.me/2012/04/24/os161-coremap/index.html#.Y4wqAOzMI0R
*/
paddr_t coremap_base_address;
typedef struct coremap_entry{
    int is_busy;
	int num_alloced_pages;
	vaddr_t virtual_address; 
} coremap_entry;


/*The actual core map which consists of all entries and there is a mutex lock protecting it*/
struct coremap_entry *coremap_entries; 

/*Same as it is in dumbvm*/
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
      
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	/*Same as it is in dumbvm*/
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

/**
 * Intilizate our vm by intilizing the coremap and
 * all the coremap entries
 */
void
vm_bootstrap(void)
{
    int coremap_size,coremap_pages;

    /**
     * We first get the total nuber of pages needed by
     * checking all addresses from start to the end
     * we do a round up at the end for total page num
     */
	paddr_t start_addr = ram_getsize();
	paddr_t termination_addr = ram_getfirstfree();
	num_pages = ((start_addr - termination_addr) / PAGE_SIZE);
    num_pages += 1; 

    /**
     * We then get the base address for our coremap
     * do address translation from PADDR to that base
     * adress whcih is a VADDR
     */
	paddr_t termination_addr_offset = termination_addr % PAGE_SIZE;
	coremap_base_address = termination_addr + PAGE_SIZE - termination_addr_offset;
	coremap_entries = (struct coremap_entry*) PADDR_TO_KVADDR(coremap_base_address);

    /*Compute the total number of coremap pages, which is used as bound later*/
	coremap_size = num_pages * sizeof(struct coremap_entry);
	coremap_pages = (coremap_size / PAGE_SIZE );
    coremap_pages += 1; /*round up page num*/

    /*Set all entries in bound as busy entries first*/
    int entry_counter_busy = 0;
    while(entry_counter_busy < num_pages)
    {
        coremap_entries[entry_counter_busy].is_busy = (entry_counter_busy < coremap_pages) ? true : false;
		entry_counter_busy += 1;
    }
	/*We set coremap_initialized to true at the end to avoid conflict*/
	coremap_initialized = true;
}

/**
 * Allocation method for our pages, we will allocate all the free pages
 * (non-busy pages) and handle page fault accordinly
 */
vaddr_t 
alloc_kpages(unsigned npages) 
{
	spinlock_acquire(&coremap_lock);
	if(coremap_initialized == false){
        /*Act as a page fault handler*/
		/*adapted from getppages() in dumbvm*/
		paddr_t target_addr = ram_stealmem(npages);
		vaddr_t return_addr = PADDR_TO_KVADDR(target_addr);
		spinlock_release(&coremap_lock);
		return return_addr;
	}

    int dest_index, num_page_avaliable;

    /**We will replace dest index with any of the free page entries
       available for our allocation */
	dest_index = invalid;
	num_page_avaliable = npages;
	
    int counter_free_entries = 0;
	while(counter_free_entries < num_pages){
		if (coremap_entries[counter_free_entries].is_busy == false){
			num_page_avaliable--;
            /*Replace all avialble entries with the destination index*/
            dest_index = (dest_index == invalid) ? counter_free_entries : dest_index;
			if(num_page_avaliable == 0){
				break;
			}
		}else {
            int total_pages = counter_free_entries + (int)npages;
			if(total_pages > num_pages){
				/*this is the case where there are not enough pages to allocate*/
				break;
			}
            /*Reset the destination and page number available*/
			num_page_avaliable = npages;
			dest_index = invalid;
		}
		counter_free_entries += 1;
	}

	if (num_page_avaliable > 0) {
		spinlock_release(&coremap_lock);
		return 0;
	}

    int translation_start = 0;
    int translation_dest;
    while(translation_start < (int) npages){
        translation_dest = translation_start++ + dest_index;
        coremap_entries[translation_dest].is_busy = true;
    }

	spinlock_release(&coremap_lock);
	
	/* compute targer_address*/
	coremap_entries[dest_index].num_alloced_pages = npages;

	paddr_t targer_addr = coremap_base_address + (dest_index * PAGE_SIZE);
	as_zero_region(targer_addr, npages);
	vaddr_t return_addr = PADDR_TO_KVADDR(targer_addr);

	coremap_entries[dest_index].virtual_address = return_addr;

	return return_addr;
}

/**
 * Method for freeing the pages
 */
void
free_kpages(vaddr_t addr)
{   
    int index = invalid;

	spinlock_acquire(&coremap_lock);
	int vaddr_counter=0;
	/*Find the target address in our coremap_entries*/
	while(vaddr_counter < num_pages){
		if(coremap_entries[vaddr_counter].virtual_address == addr){
			index = vaddr_counter;
			break;
		}
		vaddr_counter += 1;
	}
	if(index == invalid){
		spinlock_release(&coremap_lock);
		return;
	}else{
		/*"free" all target entries*/
        int vaddr_counter2= 0;
		int num_alloced_pages = coremap_entries[index].num_alloced_pages;
		while(vaddr_counter2 < num_alloced_pages){
            int new_dest = vaddr_counter2++ + index;
			coremap_entries[new_dest].is_busy = false;
		}
		spinlock_release(&coremap_lock);
		return;
	}
}


/* Following functions are same as in dumbvm.c */
void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;

	return as;
}

void
as_destroy(struct addrspace *as)
{
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return ENOSYS;
}

int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = alloc_kpages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = alloc_kpages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = alloc_kpages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);

	*ret = new;
	return 0;
}