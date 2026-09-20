#define STACK_SIZE 4096
#define NCPU 8

.section .text.entry
.global _entry

_entry:
    la sp, CPU_stack

    li t0, STACK_SIZE
    csrr t1, mhartid
    addi t1, t1, 1
    mul t0, t0, t1
    add sp, sp, t0

    call start

_spin:
    j _spin

.section .bss
.align 12
.global CPU_stack
CPU_stack:
    .space STACK_SIZE * NCPU
