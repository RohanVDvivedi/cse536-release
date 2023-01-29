#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "buf.h"
#include "elf.h"

#include <stdbool.h>

struct elfhdr* kernel_elfhdr;
struct proghdr* kernel_phdr;

uint64 find_kernel_load_addr(void) {
    // CSE 536: task 2.5.1
    kernel_elfhdr = (void*)RAMDISK;
    kernel_phdr = ((void*)RAMDISK) + kernel_elfhdr->phoff + kernel_elfhdr->phentsize;
    return kernel_phdr->vaddr;
}

uint64 find_kernel_load_file_offset(void) {
    // CSE 536: task 2.5.1
    kernel_elfhdr = (void*)RAMDISK;
    kernel_phdr = ((void*)RAMDISK) + kernel_elfhdr->phoff + kernel_elfhdr->phentsize;
    return kernel_phdr->off;
}

uint64 find_kernel_size(void) {
    // CSE 536: task 2.5.2
    kernel_elfhdr = (void*)RAMDISK;
    kernel_phdr = ((void*)RAMDISK) + kernel_elfhdr->phoff + kernel_elfhdr->phentsize;
    return kernel_phdr->filesz;
}

uint64 find_kernel_bss_size(void) {
    // CSE 536: task 2.5.2
    kernel_elfhdr = (void*)RAMDISK;
    kernel_phdr = ((void*)RAMDISK) + kernel_elfhdr->phoff + kernel_elfhdr->phentsize;
    return kernel_phdr->memsz - kernel_phdr->filesz;
}

uint64 find_kernel_entry_addr(void) {
    // CSE 536: task 2.5.3
    kernel_elfhdr = (void*)RAMDISK;
    return kernel_elfhdr->entry;
}