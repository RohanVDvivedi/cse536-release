# _entry should be the first function invoked when
# the QEMU CPU starts executing. 

.section .text
.global _entry
_entry:
        # CSE 536: Task 2.3
        # Load the end of bl_stack to the sp register

        # As given in bootloader/param.h file
        # We assume that there can be NCPU number of CPUs
        # and each CPU gets stack of size STSIZE
        # both NCPU and STSIZE are macros defines in param.h
        #
        # after the below code executes we have
        # (stack pointer) sp of ith CPU = &(bl_stack[STSIZE * (NCPU - i)])

        .equ NCPU, 8
        .equ STSIZE, 4096

        la sp, bl_stack
        csrr a0,mhartid
        addi a2,a0,-NCPU
        sub a2,zero,a2
        li a3,STSIZE
        mul a2,a2,a3
        add sp,sp,a2

        # jump to start code
        jal start

.global spin
spin:
        j spin