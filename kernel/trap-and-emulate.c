#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"


int is_readonly(int code)
{
    return (((code >> 10) & 0b11) == 0b11);
}

#define U_MODE_REG 0b00
#define S_MODE_REG 0b01
#define M_MODE_REG 0b11

int get_mode(int code)
{
    return ((code >> 8) & 0b11);
}

// Struct to keep VM registers (Sample; feel free to change.)
struct vm_reg {
    int     code; // the register's code is enough to get the information about (readable+writable OR readonly) and about (mode in which it is accessible)
    uint64  val;
};

// Keep the virtual state of the VM's privileged registers
struct vm_virtual_state {
    // User trap setup
    #define USER_TRAP_SETUP_REGS_COUNT 3
    #define USER_TRAP_SETUP_REGS_STATE_STRUCT_OFFSET 0

    // User trap handling
    #define USER_TRAP_HANDLING_REGS_COUNT 5
    #define USER_TRAP_HANDLING_REGS_STATE_STRUCT_OFFSET (USER_TRAP_SETUP_REGS_STATE_STRUCT_OFFSET + USER_TRAP_SETUP_REGS_COUNT)

    // Supervisor trap setup
    #define SUPERVISOR_TRAP_SETUP_REGS_COUNT 6
    #define SUPERVISOR_TRAP_SETUP_REGS_STATE_STRUCT_OFFSET (USER_TRAP_HANDLING_REGS_STATE_STRUCT_OFFSET + USER_TRAP_HANDLING_REGS_COUNT)

    // Supervisor trap handling
    #define SUPERVISOR_TRAP_HANDLING_REGS_COUNT 5
    #define SUPERVISOR_TRAP_HANDLING_REGS_STATE_STRUCT_OFFSET (SUPERVISOR_TRAP_SETUP_REGS_STATE_STRUCT_OFFSET + SUPERVISOR_TRAP_SETUP_REGS_COUNT)

    // Supervisor page table register
    #define SUPERVISOR_PAGE_TABLE_REGS_COUNT 1
    #define SUPERVISOR_PAGE_TABLE_REGS_STATE_STRUCT_OFFSET (SUPERVISOR_TRAP_HANDLING_REGS_STATE_STRUCT_OFFSET + SUPERVISOR_TRAP_HANDLING_REGS_COUNT)

    // Machine information registers
    #define MACHINE_INFORMATION_REGS_COUNT 4
    #define MACHINE_INFORMATION_REGS_STATE_STRUCT_OFFSET (SUPERVISOR_PAGE_TABLE_REGS_STATE_STRUCT_OFFSET + SUPERVISOR_PAGE_TABLE_REGS_COUNT)

    // Machine trap setup registers
    #define MACHINE_TRAP_SETUP_REGS_COUNT 8
    #define MACHINE_TRAP_SETUP_REGS_STATE_STRUCT_OFFSET (MACHINE_INFORMATION_REGS_STATE_STRUCT_OFFSET + MACHINE_INFORMATION_REGS_COUNT)

    // Machine trap handling registers
    #define MACHINE_TRAP_HANDLING_REGS_COUNT 7
    #define MACHINE_TRAP_HANDLING_REGS_STATE_STRUCT_OFFSET (MACHINE_TRAP_SETUP_REGS_STATE_STRUCT_OFFSET + MACHINE_TRAP_SETUP_REGS_COUNT)

    #define TOTAL_REGS_IN_STATE (MACHINE_TRAP_HANDLING_REGS_STATE_STRUCT_OFFSET + MACHINE_TRAP_HANDLING_REGS_COUNT)

    struct vm_reg vm_regs[TOTAL_REGS_IN_STATE];
};

void trap_and_emulate(void) {
    /* Comes here when a VM tries to execute a supervisor instruction. */

    /* Current process struct */
    struct proc *p = myproc();

    // get virtual address of the faulting instruction
    uint64 virt_addr_instr = r_sepc();
    // physical address of the faulting instruction
    uint64 phy_addr_instr = walkaddr(p->pagetable, virt_addr_instr) | (virt_addr_instr & ((1 << 12) - 1));

    // faulting instruction
    uint32 instr = *((uint32*)(phy_addr_instr));

    printf("va = %p, pa = %p, instruction = %p\n", virt_addr_instr, phy_addr_instr, (uint64)instr);

    // all system instructions are I-type instructions
    uint32 op       = ((instr >>  0) & ((1 <<  7) - 1));
    uint32 rd       = ((instr >>  7) & ((1 <<  5) - 1));
    uint32 funct3   = ((instr >> 12) & ((1 <<  3) - 1));
    uint32 rs1      = ((instr >> 15) & ((1 <<  5) - 1));
    uint32 upper    = ((instr >> 20) & ((1 << 12) - 1));

    printf("funct3 = %x\n", funct3);
    printf("[PI] op = %x, rd = %x, rs1 = %x, upper = %x\n", op, rd, rs1, upper);
}

void trap_and_emulate_init(void) {
    /* Create and initialize all state for the VM */
}