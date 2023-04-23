#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// intial mvendorid = "cse536" => 63 73 65 35 33 36
#define INTIAL_MVENDORID 0x637365353336

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
typedef struct vm_reg vm_reg;
struct vm_reg {
    int     code; // the register's code is enough to get the information about (readable+writable OR readonly) and about (mode in which it is accessible)
    uint64  val;
};

// Keep the virtual state of the VM's privileged registers
typedef struct vm_virtual_state vm_virtual_state;
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
    #define MACHINE_TRAP_SETUP_REGS_COUNT 7
    #define MACHINE_TRAP_SETUP_REGS_STATE_STRUCT_OFFSET (MACHINE_INFORMATION_REGS_STATE_STRUCT_OFFSET + MACHINE_INFORMATION_REGS_COUNT)

    // Machine trap handling registers
    #define MACHINE_TRAP_HANDLING_REGS_COUNT 7
    #define MACHINE_TRAP_HANDLING_REGS_STATE_STRUCT_OFFSET (MACHINE_TRAP_SETUP_REGS_STATE_STRUCT_OFFSET + MACHINE_TRAP_SETUP_REGS_COUNT)

    #define TOTAL_REGS_IN_STATE (MACHINE_TRAP_HANDLING_REGS_STATE_STRUCT_OFFSET + MACHINE_TRAP_HANDLING_REGS_COUNT)

    struct vm_reg vm_regs[TOTAL_REGS_IN_STATE];

    int current_privilege_mode;
};

vm_reg* get_register_by_code(vm_virtual_state* vvs, int code)
{
    for(int i = 0; i < TOTAL_REGS_IN_STATE; i++)
    {
        if(vvs->vm_regs[i].code == code)
            return &(vvs->vm_regs[i]);
    }
    return NULL;
}

uint64* get_nth_unpriv_register_from_trapframe(struct proc* p, int nth)
{
    if(nth == 0 || nth > 31)
        return NULL;
    return (&(p->trapframe->ra)) + (nth-1);
}

vm_virtual_state global_vmm_state;

// opcode
#define SYSTEM 0x73

// funct3
#define ECALL_OR_EBREAK  0x0
#define CSRRW  0x1
#define CSRRS  0x2
#define CSRRC  0x3
#define CSRRWI 0x5
#define CSRRSI 0x6
#define CSRRCI 0x7

// upper
#define ECALL  0x0
#define EBREAK 0x1
#define SRET   0x102
#define MRET   0x302

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

    // all system instructions are I-type instructions
    uint32 op       = ((instr >>  0) & ((1 <<  7) - 1));
    uint32 rd       = ((instr >>  7) & ((1 <<  5) - 1));
    uint32 funct3   = ((instr >> 12) & ((1 <<  3) - 1));
    uint32 rs1      = ((instr >> 15) & ((1 <<  5) - 1));
    uint32 upper    = ((instr >> 20) & ((1 << 12) - 1));

    printf("funct3 = %x\n", funct3);
    printf("[PI] op = %x, rd = %x, rs1 = %x, upper = %x\n", op, rd, rs1, upper);

    // if not a system opcode
    if(op != SYSTEM)
    {
        setkilled(p);
        return;
    }

    switch(funct3)
    {
        case ECALL_OR_EBREAK :
        {
            if(rd == 0 && rs1 == 0 && upper == 0) // then it is ecall -> else if (upper == 1) then it is ebreak
            {

            }
            else
            {
                setkilled(p);
                return;
            }
            break;
        }
        case CSRRW :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, upper);
            if(csr_p == NULL || global_vmm_state.current_privilege_mode >= get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                return;
            }
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            if(rs1 == 0)
                csr_p->val = 0;
            else
            {
                uint64* rs1_p = get_nth_unpriv_register_from_trapframe(p, rs1);
                csr_p->val = (*rs1_p);
            }
            break;
        }
        case CSRRS :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, upper);
            if(csr_p == NULL || global_vmm_state.current_privilege_mode >= get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                return;
            }
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            if(rs1 != 0)
            {
                uint64* rs1_p = get_nth_unpriv_register_from_trapframe(p, rs1);
                csr_p->val |= (*rs1_p);
            }
            break;
        }
        case CSRRC :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, upper);
            if(csr_p == NULL || global_vmm_state.current_privilege_mode >= get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                return;
            }
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            if(rs1 != 0)
            {
                uint64* rs1_p = get_nth_unpriv_register_from_trapframe(p, rs1);
                csr_p->val &= (~(*rs1_p));
            }
            break;
        }
        case CSRRWI :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, upper);
            if(csr_p == NULL || global_vmm_state.current_privilege_mode >= get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                return;
            }
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            csr_p->val = rs1;
            break;
        }
        case CSRRSI :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, upper);
            if(csr_p == NULL || global_vmm_state.current_privilege_mode >= get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                return;
            }
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            csr_p->val |= rs1;
            break;
        }
        case CSRRCI :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, upper);
            if(csr_p == NULL || global_vmm_state.current_privilege_mode >= get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                return;
            }
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            csr_p->val &= (~rs1);
            break;
        }
        default :
        {
            setkilled(p);
            return;
        }
    }
}

void trap_and_emulate_init(void) {
    global_vmm_state = (vm_virtual_state){
        .vm_regs = {
            // User trap setup
            {.code = 0x000, .val = 0},
            {.code = 0x004, .val = 0},
            {.code = 0x005, .val = 0},

            // User trap handling
            {.code = 0x040, .val = 0},
            {.code = 0x041, .val = 0},
            {.code = 0x042, .val = 0},
            {.code = 0x043, .val = 0},
            {.code = 0x044, .val = 0},

            // Supervisor trap setup
            {.code = 0x100, .val = 0},
            {.code = 0x102, .val = 0},
            {.code = 0x103, .val = 0},
            {.code = 0x104, .val = 0},
            {.code = 0x105, .val = 0},
            {.code = 0x106, .val = 0},

            // Supervisor trap handling
            {.code = 0x140, .val = 0},
            {.code = 0x141, .val = 0},
            {.code = 0x142, .val = 0},
            {.code = 0x143, .val = 0},
            {.code = 0x144, .val = 0},

            // Supervisor page table register
            {.code = 0x180, .val = 0},

            // Machine information registers
            {.code = 0xf11, .val = INTIAL_MVENDORID}, // intial mvendorid = "cse536" => 63 73 65 35 33 36
            {.code = 0xf12, .val = 0},
            {.code = 0xf13, .val = 0},
            {.code = 0xf14, .val = 0},

            // Machine trap setup registers
            {.code = 0x300, .val = 0},
            {.code = 0x301, .val = 0},
            {.code = 0x302, .val = 0},
            {.code = 0x303, .val = 0},
            {.code = 0x304, .val = 0},
            {.code = 0x305, .val = 0},
            {.code = 0x306, .val = 0},

            // Machine trap handling registers
            {.code = 0x340, .val = 0},
            {.code = 0x341, .val = 0},
            {.code = 0x342, .val = 0},
            {.code = 0x343, .val = 0},
            {.code = 0x344, .val = 0},
            {.code = 0x34a, .val = 0},
            {.code = 0x34b, .val = 0},
        },
        .current_privilege_mode = M_MODE_REG,
    };
}