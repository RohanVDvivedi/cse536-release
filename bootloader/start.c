/* These files have been taken from the open-source xv6 Operating System codebase (MIT License).  */
/* Modifications made by Adil Ahmad. */

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "buf.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char bl_stack[STSIZE * NCPU];

// Task: Collect and store hardware information
struct sys_info {
  // HW info collected from CPU registers
  uint64 vendor;
  uint64 arch;
  uint64 impl;

  // Bootloader binary addresses
  uint64 bl_start;
  uint64 bl_end;

  // Accessible DRAM addresses
  uint64 dr_start;
  uint64 dr_end;
};
struct sys_info* sys_info_ptr;

extern void _entry(void);

// entry.S jumps here in machine mode on stack0.
void
start()
{
  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);

  // set M Previous Privilege mode to Supervisor, for mret.
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // disable paging for now.
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // CSE 536: Task 2.4
  //  Enable R/W/X access to all parts of the address space, 
  //  except for the upper 10 MB (0 - 117 MB) using PMP
  w_pmpaddr0((0x80000000ULL + (117ULL * 1024ULL * 1024ULL))>>2); 
  w_pmpcfg0(0xfULL);

  // CSE 536: Task 2.5
  // Load the kernel binary to its correct location
  uint64 kernel_entry_addr = 0;
  uint64 kernel_load_addr  = 0;
  uint64 kernel_size       = 0;

  // address of the kernel to copy from
  uint64 kernel_load_file_offset = find_kernel_load_file_offset();

  // CSE 536: Task 2.5.1
  // Find the loading address of the kernel binary
  kernel_load_addr  = find_kernel_load_addr();

  // CSE 536: Task 2.5.2
  // Find the kernel binary size and copy it to the load address
  kernel_size       = find_kernel_size();
  // kernel_size is the size of the kernel without its bss section

  // load kernel to desired address
  for(uint64 off = 0; off < kernel_size;)
  {
    struct buf b= {};
    b.blockno = (kernel_load_file_offset + off) / BSIZE;
    kernel_copy(&b);
    #define min(a,b) ((a)<(b))?(a):(b)
    uint to_copy = min(BSIZE, kernel_size - off);
    memmove((void*)(kernel_load_addr + off), b.data, to_copy);
    off+=to_copy;
  }

  // get bss size of the kernel
  uint64 kernel_bss_size       = find_kernel_bss_size();

  // set bss for the kernel to all 0s
  memset((void*)(kernel_load_addr + kernel_size), 0, kernel_bss_size);

  // CSE 536: Task 2.5.3
  // Find the entry address and write it to mepc
  kernel_entry_addr = find_kernel_entry_addr();
  w_mepc(kernel_entry_addr);

  // CSE 536: Task 2.6
  // Provide system information to the kernel
  sys_info_ptr = (void*)(0x80080000);
  sys_info_ptr->vendor = r_mvendorid();
  sys_info_ptr->arch = r_marchid();
  sys_info_ptr->impl = r_mimpid();
  sys_info_ptr->bl_start = 0x80000000;
  sys_info_ptr->bl_end = end;
  sys_info_ptr->dr_start = KERNBASE;
  sys_info_ptr->dr_end = PHYSTOP;

  // CSE 536: Task 2.5.3
  // Jump to the OS kernel code
  asm volatile("mret");
}