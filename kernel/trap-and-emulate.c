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

// some important register codes
#define USTATUS 0x000
#define SSTATUS 0x100
#define MSTATUS 0x300

#define UTVEC   0x005
#define STVEC   0x105
#define MTVEC   0x305

#define UEPC    0x041
#define SEPC    0x141
#define MEPC    0x341

#define UCAUSE  0x042
#define SCAUSE  0x142
#define MCAUSE  0x342

#define MEDELEG 0x302

#define MVENDORID 0xf11

#define PMPCFG0  0x3a0
#define PMPADDR0 0x3b0

// status bits
#define SIE_FL  (0x1ULL <<  1)
#define MIE_FL  (0x1ULL <<  3)
#define SPIE_FL (0x1ULL <<  5)
#define MPIE_FL (0x1ULL <<  7)
#define SPP_FL  (0x1ULL <<  8)
#define MPP_FL  (0x3ULL << 11)

int get_mode(int code)
{
    if(code == MVENDORID)
        return U_MODE_REG;
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

    // Machine memory protection
    #define MACHINE_MEMORY_PROTECTION_REGS_COUNT 2
    #define MACHINE_MEMORY_PROTECTION_REGS_STATE_STRUCT_OFFSET (MACHINE_TRAP_HANDLING_REGS_STATE_STRUCT_OFFSET + MACHINE_TRAP_HANDLING_REGS_COUNT)

    #define TOTAL_REGS_IN_STATE (MACHINE_MEMORY_PROTECTION_REGS_STATE_STRUCT_OFFSET + MACHINE_MEMORY_PROTECTION_REGS_STATE_STRUCT_OFFSET)

    struct vm_reg vm_regs[TOTAL_REGS_IN_STATE];

    int current_privilege_mode;

    pagetable_t M_mode_pagetable;
    pagetable_t S_U_mode_pagetable;
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
#define ECALL_EBREAK_SRET_MRET  0x0
#define CSRRW  0x1
#define CSRRS  0x2
#define CSRRC  0x3
#define CSRRWI 0x5
#define CSRRSI 0x6
#define CSRRCI 0x7

// uimm
#define ECALL  0x0
#define EBREAK 0x1
#define SRET   0x102
#define MRET   0x302

// pmp enabled regions in guest's address space
#define PMP_REGION_START 0x80000000
#define PMP_REGION_END   0x80400000

void duplicate_pagetable_for_vm_RECURSIVE(pagetable_t to_be_duplicated, pagetable_t duplicate, int level)
{
    if(level == 0)
    {
        memmove(duplicate, to_be_duplicated, PGSIZE);
        return;
    }
    for(int i = 0; i < 512; i++)
    {
        pte_t pte = to_be_duplicated[i];
        if(pte & PTE_V)
        {
            void* new_child = kalloc();
            memset(new_child, 0, PGSIZE);
            uint64 new_child_addr = (uint64) new_child;
            int perm = pte & (PTE_R | PTE_W | PTE_W | PTE_U);
            duplicate[i] = PA2PTE(new_child_addr) | perm | PTE_V;
            duplicate_pagetable_for_vm_RECURSIVE((pagetable_t) PTE2PA(to_be_duplicated[i]), (pagetable_t) PTE2PA(duplicate[i]), level - 1);
        }
    }
}

void duplicate_pagetable_for_vm(pagetable_t* to_be_duplicated, pagetable_t* duplicate)
{
    *duplicate = uvmcreate();
    duplicate_pagetable_for_vm_RECURSIVE(*to_be_duplicated, *duplicate, 2);
}

void set_permissions_in_pagetable(pagetable_t pt, int perm, uint64 start_va, uint64 size)
{
    for(uint64 va = start_va; va < (start_va + size); va += PGSIZE)
    {
        pte_t * pte_p = walk(pt, va, 0);
        if(pte_p == NULL)
            continue;
        (*pte_p) = ((*pte_p) & ( ~(PTE_R | PTE_W | PTE_X) )) | perm;
    }
}

void destroy_S_U_mode_pagetable_for_vm(pagetable_t pagetable, int l)
{
  if(l != 0)
  {
    // there are 2^9 = 512 PTEs in a page table.
    for(int i = 0; i < 512; i++){
        pte_t pte = pagetable[i];
        if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
            // this PTE points to a lower-level page table.
            uint64 child = PTE2PA(pte);
            destroy_S_U_mode_pagetable_for_vm((pagetable_t)child, l-1);
            pagetable[i] = 0;
        }
    }
  }
  kfree((void*)pagetable);
}

void trap_and_emulate_ecall(void) {
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
    uint32 uimm     = ((instr >> 20) & ((1 << 12) - 1));

    if(op == SYSTEM && rd == 0 && funct3 == 0 && rs1 == 0 && uimm == 0)
    {
        printf("(EC at %p)\n", p->trapframe->epc);

        int go_to_machine_mode = 1;
        if( (global_vmm_state.current_privilege_mode == S_MODE_REG && (get_register_by_code(&global_vmm_state, MEDELEG)->val >> 1) & 1) ||
            (global_vmm_state.current_privilege_mode == U_MODE_REG && (get_register_by_code(&global_vmm_state, MEDELEG)->val >> 3) & 1) )
            go_to_machine_mode = 0;

        // go to m mode
        if(go_to_machine_mode)
        {
            // move pc to mepc and mtvec to pc
            get_register_by_code(&global_vmm_state, MEPC)->val = p->trapframe->epc;
            p->trapframe->epc = get_register_by_code(&global_vmm_state, MTVEC)->val;

            // set mcause register
            get_register_by_code(&global_vmm_state, MCAUSE)->val = (global_vmm_state.current_privilege_mode + 8);

            // move MIE bit to MPIE bit and clear MIE bit
            vm_reg* mstatus_p = get_register_by_code(&global_vmm_state, MSTATUS);
            uint64 MIE_bit = mstatus_p->val & MIE_FL;
            mstatus_p->val &= (~MIE_FL);
            mstatus_p->val &= (~MPIE_FL);
            mstatus_p->val |= (MIE_bit << 4);

            // make MPP = 01 (s mode)
            mstatus_p->val &= (~MPP_FL);
            mstatus_p->val |= ((((uint64)global_vmm_state.current_privilege_mode) << 11) & MPP_FL);

            // change mode to M mode
            global_vmm_state.current_privilege_mode = M_MODE_REG;

            // set pagetable to M_mode page table
            p->pagetable = global_vmm_state.M_mode_pagetable;
        }
        // go to s mode
        else
        {
            // move pc to sepc and stvec to pc
            get_register_by_code(&global_vmm_state, SEPC)->val = p->trapframe->epc;
            p->trapframe->epc = get_register_by_code(&global_vmm_state, STVEC)->val;

            // set scause register
            get_register_by_code(&global_vmm_state, SCAUSE)->val = (global_vmm_state.current_privilege_mode + 8);

            // move SIE bit to SPIE bit, and clear SIE bit
            vm_reg* sstatus_p = get_register_by_code(&global_vmm_state, SSTATUS);
            uint64 SIE_bit = sstatus_p->val & SIE_FL;
            sstatus_p->val &= (~SIE_FL);
            sstatus_p->val &= (~SPIE_FL);
            sstatus_p->val |= (SIE_bit << 4);

            // make SPP = 0 (u mode)
            sstatus_p->val &= (~SPP_FL);
            sstatus_p->val |= ((((uint64)global_vmm_state.current_privilege_mode) << 8) & SPP_FL);

            // change mode to S mode
            global_vmm_state.current_privilege_mode = S_MODE_REG;

            // set pagetable to S_U_mode page table
            p->pagetable = global_vmm_state.S_U_mode_pagetable;
        }
    }
    else
    {
        setkilled(p);
        goto PROCESS_KILLED_ecall;
    }

    return;

    // come here only when the process is killed
    PROCESS_KILLED_ecall:;

    // set process pge table to M_mode_pagetable
    p->pagetable = global_vmm_state.M_mode_pagetable;

    // and release all pages of S_U_mode_pagetable
    destroy_S_U_mode_pagetable_for_vm(global_vmm_state.S_U_mode_pagetable, 2);

    // reset the VMM state
    trap_and_emulate_init();

    return;
}

/*static void illegal_instruction_in_usermode(void)
{
    struct proc *p = myproc();

    p->trapframe->epc = get_register_by_code(&global_vmm_state, STVEC)->val;

    get_register_by_code(&global_vmm_state, SEPC)->val = r_sepc();

    get_register_by_code(&global_vmm_state, SCAUSE)->val = 2;

    global_vmm_state.current_privilege_mode = S_MODE_REG;
}*/

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
    uint32 uimm     = ((instr >> 20) & ((1 << 12) - 1));

    /* Print the statement */
    printf("(PI at %p) op = %x, rd = %x, funct3 = %x, rs1 = %x, uimm = %x\n", 
                virt_addr_instr, op, rd, funct3, rs1, uimm);

    // if it is the first instruction, then set the M mode pagetable
    if(global_vmm_state.M_mode_pagetable == NULL && global_vmm_state.S_U_mode_pagetable == NULL)
    {
        global_vmm_state.M_mode_pagetable = p->pagetable;
        duplicate_pagetable_for_vm(&(global_vmm_state.M_mode_pagetable), &(global_vmm_state.S_U_mode_pagetable));
        set_permissions_in_pagetable(global_vmm_state.S_U_mode_pagetable, 0, PMP_REGION_START, PMP_REGION_END - PMP_REGION_START);
    }

    // if not a system opcode
    if(op != SYSTEM)
    {
        setkilled(p);
        goto PROCESS_KILLED;
    }

    int wirte_to_pmp_regs = 0;

    switch(funct3)
    {
        case ECALL_EBREAK_SRET_MRET :
        {
            if(rd == 0 && rs1 == 0)
            {
                switch(uimm)
                {
                    case SRET :
                    {
                        // forward it to guest VM's handler
                        /*if(global_vmm_state.current_privilege_mode == U_MODE_REG)
                        {
                            illegal_instruction_in_usermode();
                            break;
                        }*/

                        if(global_vmm_state.current_privilege_mode != S_MODE_REG)
                        {
                            setkilled(p);
                            goto PROCESS_KILLED;
                        }

                        // get the new privilege mode to return to
                        vm_reg* sstatus_p = get_register_by_code(&global_vmm_state, SSTATUS);
                        int new_priv_mode = (sstatus_p->val & SPP_FL) >> 8;

                        // set MPP to 0
                        sstatus_p->val &= (~SPP_FL);

                        // move MPIE to MIE and clear MPIE
                        uint64 SPIE_bit = sstatus_p->val & SPIE_FL;
                        sstatus_p->val &= (~SIE_FL);
                        sstatus_p->val &= (~SPIE_FL);
                        sstatus_p->val |= (SPIE_bit >> 4);

                        // move mepc to pc
                        p->trapframe->epc = get_register_by_code(&global_vmm_state, SEPC)->val;

                        // update the current privilege mode
                        global_vmm_state.current_privilege_mode = new_priv_mode;

                        // set pagetable based on current_privilege_mode
                        p->pagetable = (global_vmm_state.current_privilege_mode == M_MODE_REG) ? global_vmm_state.M_mode_pagetable : global_vmm_state.S_U_mode_pagetable;
                        break;
                    }
                    case MRET :
                    {
                        // forward it to guest VM's handler
                        /*
                        if(global_vmm_state.current_privilege_mode == U_MODE_REG)
                        {
                            illegal_instruction_in_usermode();
                            break;
                        }*/

                        if(global_vmm_state.current_privilege_mode != M_MODE_REG)
                        {
                            setkilled(p);
                            goto PROCESS_KILLED;
                        }

                        // get the new privilege mode to return to
                        vm_reg* mstatus_p = get_register_by_code(&global_vmm_state, MSTATUS);
                        int new_priv_mode = (mstatus_p->val & MPP_FL) >> 11;

                        // set MPP to 0
                        mstatus_p->val &= (~MPP_FL);

                        // move MPIE to MIE and clear MPIE
                        uint64 MPIE_bit = mstatus_p->val & MPIE_FL;
                        mstatus_p->val &= (~MIE_FL);
                        mstatus_p->val &= (~MPIE_FL);
                        mstatus_p->val |= (MPIE_bit >> 4);

                        // move mepc to pc
                        p->trapframe->epc = get_register_by_code(&global_vmm_state, MEPC)->val;

                        // update the current privilege mode
                        global_vmm_state.current_privilege_mode = new_priv_mode;

                        // set pagetable based on current_privilege_mode
                        p->pagetable = (global_vmm_state.current_privilege_mode == M_MODE_REG) ? global_vmm_state.M_mode_pagetable : global_vmm_state.S_U_mode_pagetable;
                        break;
                    }
                    default :
                    {
                        setkilled(p);
                        goto PROCESS_KILLED;
                    }
                }
            }
            else
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            break;
        }
        case CSRRW :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, uimm);
            if(csr_p == NULL)
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            if(global_vmm_state.current_privilege_mode < get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            /*{
                illegal_instruction_in_usermode();
                break;
            }*/
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
            wirte_to_pmp_regs = (uimm == PMPCFG0) || (uimm == PMPADDR0);
            p->trapframe->epc += 4;
            break;
        }
        case CSRRS :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, uimm);
            if(csr_p == NULL)
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            if(global_vmm_state.current_privilege_mode < get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            /*{
                illegal_instruction_in_usermode();
                break;
            }*/
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            if(rs1 != 0)
            {
                uint64* rs1_p = get_nth_unpriv_register_from_trapframe(p, rs1);
                csr_p->val |= (*rs1_p);
                wirte_to_pmp_regs = (uimm == PMPCFG0) || (uimm == PMPADDR0);
            }
            p->trapframe->epc += 4;
            break;
        }
        case CSRRC :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, uimm);
            if(csr_p == NULL)
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            if(global_vmm_state.current_privilege_mode < get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            /*{
                illegal_instruction_in_usermode();
                break;
            }*/
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            if(rs1 != 0)
            {
                uint64* rs1_p = get_nth_unpriv_register_from_trapframe(p, rs1);
                csr_p->val &= (~(*rs1_p));
                wirte_to_pmp_regs = (uimm == PMPCFG0) || (uimm == PMPADDR0);
            }
            p->trapframe->epc += 4;
            break;
        }
        case CSRRWI :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, uimm);
            if(csr_p == NULL)
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            if(global_vmm_state.current_privilege_mode < get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            /*{
                illegal_instruction_in_usermode();
                break;
            }*/
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            csr_p->val = rs1;
            wirte_to_pmp_regs = (uimm == PMPCFG0) || (uimm == PMPADDR0);
            p->trapframe->epc += 4;
            break;
        }
        case CSRRSI :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, uimm);
            if(csr_p == NULL)
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            if(global_vmm_state.current_privilege_mode < get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            /*{
                illegal_instruction_in_usermode();
                break;
            }*/
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            csr_p->val |= rs1;
            wirte_to_pmp_regs = (uimm == PMPCFG0) || (uimm == PMPADDR0);
            p->trapframe->epc += 4;
            break;
        }
        case CSRRCI :
        {
            vm_reg* csr_p = get_register_by_code(&global_vmm_state, uimm);
            if(csr_p == NULL)
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            if(global_vmm_state.current_privilege_mode < get_mode(csr_p->code)) // not emulating the requested csr register OR have lower privilege than what is required by the register
            {
                setkilled(p);
                goto PROCESS_KILLED;
            }
            /*{
                illegal_instruction_in_usermode();
                break;
            }*/
            if(rd != 0)
            {
                uint64* rd_p = get_nth_unpriv_register_from_trapframe(p, rd);
                *rd_p = csr_p->val;
            }
            csr_p->val &= (~rs1);
            wirte_to_pmp_regs = (uimm == PMPCFG0) || (uimm == PMPADDR0);
            p->trapframe->epc += 4;
            break;
        }
        default :
        {
            setkilled(p);
            goto PROCESS_KILLED;
        }
    }

    if(wirte_to_pmp_regs)
    {
        // unmap regions from 0x80000000  -  0x80400000
        vm_reg* pmpcfg0 = get_register_by_code(&global_vmm_state, PMPCFG0);
        vm_reg* pmpaddr0 = get_register_by_code(&global_vmm_state, PMPADDR0);
        set_permissions_in_pagetable(global_vmm_state.S_U_mode_pagetable, 0, PMP_REGION_START, PMP_REGION_END - PMP_REGION_START);
        if(((pmpcfg0->val >> 3) & 0x3) == 1)
            set_permissions_in_pagetable(global_vmm_state.S_U_mode_pagetable, (pmpcfg0->val & 0x7), PMP_REGION_START, pmpaddr0->val - PMP_REGION_START);
    }

    // if mvendorid is set to 0, we exit
    if(get_register_by_code(&global_vmm_state, MVENDORID) == 0)
    {
        setkilled(p);
        goto PROCESS_KILLED;
    }


    return;

    // come here only when the process is killed
    PROCESS_KILLED:;

    // set process pge table to M_mode_pagetable
    p->pagetable = global_vmm_state.M_mode_pagetable;

    // and release all pages of S_U_mode_pagetable
    destroy_S_U_mode_pagetable_for_vm(global_vmm_state.S_U_mode_pagetable, 2);

    // reset the VMM state
    trap_and_emulate_init();

    return;
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

            // Machine memory protection registers
            {.code = 0x3a0, .val = 0},
            {.code = 0x3b1, .val = 0},
        },
        .current_privilege_mode = M_MODE_REG,
        .M_mode_pagetable = NULL,
        .S_U_mode_pagetable = NULL,
    };
}