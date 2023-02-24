/* This file contains code for a generic page fault handler for processes. */
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz);
int flags2perm(int flags);

/* CSE 536: (2.4) read current time. */
uint64 read_current_timestamp() {
  uint64 curticks = 0;
  acquire(&tickslock);
  curticks = ticks;
  wakeup(&ticks);
  release(&tickslock);
  return curticks;
}

bool psa_tracker[PSASIZE];

/* All blocks are free during initialization. */
void init_psa_regions(void)
{
    for (int i = 0; i < PSASIZE; i++) 
        psa_tracker[i] = false;
}

// utility qsort
void qsort(int* arr, int size)
{
    if(size <= 1)
        return;
    int p = arr[size-1];
    int j = 0;
    for(int i = 0; i < size; i++)
    {
        if(arr[i] <= p)
        {
            int t = arr[i];
            arr[i] = arr[j];
            arr[j++] = t;
        }
    }
    qsort(arr, --j);
    qsort(arr + j + 1, size - j - 1);
}

/* Evict heap page to disk when resident pages exceed limit */
void evict_page_to_disk(struct proc* p) {
    /* Find free block */

    // find all used psa blocks
    int used_psa_blocks[MAXRESHEAP];
    int used_psa_blocks_count = 0;
    for(int i = 0; i < MAXHEAP; i++)
        // conditions for a page to be allocated and not in RAM (saved in PSA)
        if( p->heap_tracker[i].addr != 0xFFFFFFFFFFFFFFFF && 
            p->heap_tracker[i].loaded == 0 && 
            p->heap_tracker[i].startblock != -1)
            used_psa_blocks[used_psa_blocks_count++] = p->heap_tracker[i].startblock;

    // sort used psa blocks
    qsort(used_psa_blocks, used_psa_blocks_count);

    // iterate over PSA
    int blockno = PSASTART;
    for(int i = 0; blockno <= PSAEND && i < used_psa_blocks_count && blockno == used_psa_blocks[i]; blockno+=4, i++);

    // after the above loop we will have a valid blockno to write a page into

    /* Find victim page using FIFO. */
    int heap_tracker_index_victim = -1;
    uint64 last_load_time_victim = -1; // this -1 will set it to UINT64_MAX
    for(int i = 0; i < MAXHEAP; i++)
    {
        // conditions for a page being allocated and loaded in RAM (and not on PSA)
        if( p->heap_tracker[i].addr != 0xFFFFFFFFFFFFFFFF && 
            p->heap_tracker[i].loaded == 1 && 
            p->heap_tracker[i].startblock == -1)
        {
            if(last_load_time_victim > p->heap_tracker[i].last_load_time)
            {
                heap_tracker_index_victim = i;
                last_load_time_victim = p->heap_tracker[i].last_load_time;
            }
        }
    }

    /* Print statement. */
    print_evict_page(p->heap_tracker[heap_tracker_index_victim].addr, blockno);

    /* Read memory from the user to kernel memory first. */
    void* km = kalloc();
    copyin(p->pagetable, km, p->heap_tracker[heap_tracker_index_victim].addr, PGSIZE);
    
    /* Write to the disk blocks. Below is a template as to how this works. There is
     * definitely a better way but this works for now. :p */
    for(int i = 0; i < 4; i++)
    {
        struct buf* b;
        b = bread(1, PSASTART+(blockno+i));
        // Copy page contents to b.data using memmove.
        memmove(b->data, km + (BSIZE * i), BSIZE);
        bwrite(b);
        brelse(b);
    }

    kfree(km);

    /* Unmap swapped out page */
    uvmunmap(p->pagetable, p->heap_tracker[heap_tracker_index_victim].addr, 1, 1);

    /* Update the resident heap tracker. */
    p->heap_tracker[heap_tracker_index_victim].loaded = 0;
    p->heap_tracker[heap_tracker_index_victim].startblock = blockno;
}

/* Retrieve faulted page from disk. */
void retrieve_page_from_disk(struct proc* p, int heap_tracker_index, uint64 uvaddr) {
    /* Find where the page is located in disk */
    int startblock = p->heap_tracker[heap_tracker_index].startblock;

    /* Print statement. */
    print_retrieve_page(uvaddr, startblock);

    /* Create a kernel page to read memory temporarily into first. */
    void* km = kalloc();
    
    /* Read the disk block into temp kernel page. */
    for(int i = 0; i < 4; i++)
    {
        struct buf* b;
        b = bread(1, PSASTART+(startblock+i));
        memmove(km + (BSIZE * i), b->data, BSIZE);
        brelse(b);
    }

    /* Copy from temp kernel page to uvaddr (use copyout) */
    copyout(p->pagetable, uvaddr, km, PGSIZE);

    kfree(km);

    p->heap_tracker[heap_tracker_index].loaded = 1;
    p->heap_tracker[heap_tracker_index].startblock = -1;
}


void page_fault_handler(void) 
{
    /* Current process struct */
    struct proc *p = myproc();

    /* Track whether the heap page should be brought back from disk or not. */
    bool load_from_disk = false;

    /* Find faulting address. */
    uint64 faulting_addr = r_stval();
    faulting_addr = PGROUNDDOWN(faulting_addr);
    print_page_fault(p->name, faulting_addr);

    /* Check if the fault address is a heap page. Use p->heap_tracker */
    // is_heap_page_index is the index of the heap page in the heap_tracker, else it will be -1
    int is_heap_page_index = -1;
    for(int i = 0; i < MAXHEAP && is_heap_page_index == -1; i++)
        if(faulting_addr == p->heap_tracker[i].addr)
            is_heap_page_index = i;

    if (is_heap_page_index != -1) {
        goto heap_handle;
    }

    // varibales to load the correct segment
    struct elfhdr elf;
    struct inode *ip;
    struct proghdr ph;
    pagetable_t pagetable = p->pagetable;

    // get inode from the executable path and lock it
    ip = namei(p->name);
    ilock(ip);

    // read elf header
    readi(ip, 0, (uint64)&elf, 0, sizeof(elf));

    // iterate over all the program headers, to find the right one
    for(int i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
        // read the ith program header
        readi(ip, 0, (uint64)&ph, off, sizeof(ph));

        // if not loadable continue
        if(ph.type != ELF_PROG_LOAD)
            continue;

        // check if the address lies in the address range of program header
        if(ph.vaddr <= faulting_addr && faulting_addr < ph.vaddr + ph.memsz)
        {
            // uvmalloc a page worth of memory for this faulting page
            uvmalloc(pagetable, faulting_addr, faulting_addr + PGSIZE, flags2perm(ph.flags));

            // load the segment only if the page requested has contents on the file, i.e. is not an only bss section
            if(ph.vaddr <= faulting_addr && faulting_addr < ph.vaddr + ph.filesz) {
                // calucate the offset and size to read from, in the program segment, and read it
                uint off_in_ph = ph.off + (faulting_addr - ph.vaddr);
                #define min(a,b) (((a)<(b))?(a):(b))
                uint sz_in_ph = min(PGSIZE, ph.vaddr + ph.filesz - faulting_addr);
                loadseg(pagetable, faulting_addr, ip, off_in_ph, sz_in_ph);
            }
            break;
        }
    }

    iunlockput(ip);

    /* If it came here, it is a page from the program binary that we must load. */
    print_load_seg(faulting_addr, ph.vaddr, ph.memsz);

    /* Go to out, since the remainder of this code is for the heap. */
    goto out;

heap_handle:
    /* 2.4: Check if resident pages are more than heap pages. If yes, evict. */
    if (p->resident_heap_pages == MAXRESHEAP) {
        evict_page_to_disk(p);
        p->resident_heap_pages--;
    }

    /* 2.3: Map a heap page into the process' address space. (Hint: check growproc) */
    uvmalloc(p->pagetable, faulting_addr, faulting_addr + PGSIZE, PTE_W);

    /* 2.4: Update the last load time for the loaded heap page in p->heap_tracker. */
    p->heap_tracker[is_heap_page_index].last_load_time = read_current_timestamp();
    p->heap_tracker[is_heap_page_index].loaded = 1;

    /* 2.4: Heap page was swapped to disk previously. We must load it from disk. */
    load_from_disk = (p->heap_tracker[is_heap_page_index].startblock != -1);
    if (load_from_disk) {
        retrieve_page_from_disk(p, is_heap_page_index, faulting_addr);
    }

    /* Track that another heap page has been brought into memory. */
    p->resident_heap_pages++;

out:
    /* Flush stale page table entries. This is important to always do. */
    sfence_vma();
    return;
}