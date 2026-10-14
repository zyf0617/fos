.section .text
.global context_probe
context_probe:
    addi sp, sp, -128
    sd ra, 0(sp)
    sd gp, 8(sp)
    sd tp, 16(sp)
    sd s0, 24(sp)
    sd s1, 32(sp)
    sd s2, 40(sp)
    sd s3, 48(sp)
    sd s4, 56(sp)
    sd s5, 64(sp)
    sd s6, 72(sp)
    sd s7, 80(sp)
    sd s8, 88(sp)
    sd s9, 96(sp)
    sd s10, 104(sp)
    sd s11, 112(sp)
    li s0, 100
    li s1, 101
    li s2, 102
    li s3, 103
    li s4, 104
    li s5, 105
    li s6, 106
    li s7, 107
    li s8, 108
    li s9, 109
    li s10, 110
    li s11, 111
    li t0, 112
    li t1, 113
    li t2, 114
    li t3, 115
    li t4, 116
    li t5, 117
    li t6, 118
    li gp, 119
    li tp, 120
    li ra, 121
    li a1, 122
    li a2, 123
    li a3, 124
    li a4, 125
    li a5, 126
    li a6, 127
    li a7, 12345
    li a0, 42
    ecall
    sd a0, 120(sp)
    li a0, 12345
    bne a7, a0, failed
    ld a0, 120(sp)
    li a7, -1
    bne a0, a7, failed
wait_for_timer:
    li a0, 1000000
wait_loop:
    addi a0, a0, -1
    bnez a0, wait_loop
    li a7, 99
    ecall
    li a7, 3
    bltu a0, a7, wait_for_timer
    li a0, 100
    bne s0, a0, failed
    li a0, 101
    bne s1, a0, failed
    li a0, 102
    bne s2, a0, failed
    li a0, 103
    bne s3, a0, failed
    li a0, 104
    bne s4, a0, failed
    li a0, 105
    bne s5, a0, failed
    li a0, 106
    bne s6, a0, failed
    li a0, 107
    bne s7, a0, failed
    li a0, 108
    bne s8, a0, failed
    li a0, 109
    bne s9, a0, failed
    li a0, 110
    bne s10, a0, failed
    li a0, 111
    bne s11, a0, failed
    li a0, 112
    bne t0, a0, failed
    li a0, 113
    bne t1, a0, failed
    li a0, 114
    bne t2, a0, failed
    li a0, 115
    bne t3, a0, failed
    li a0, 116
    bne t4, a0, failed
    li a0, 117
    bne t5, a0, failed
    li a0, 118
    bne t6, a0, failed
    li a0, 119
    bne gp, a0, failed
    li a0, 120
    bne tp, a0, failed
    li a0, 121
    bne ra, a0, failed
    li a0, 122
    bne a1, a0, failed
    li a0, 123
    bne a2, a0, failed
    li a0, 124
    bne a3, a0, failed
    li a0, 125
    bne a4, a0, failed
    li a0, 126
    bne a5, a0, failed
    li a0, 127
    bne a6, a0, failed
    li a0, 0
    j restore
failed:
    li a0, -1
restore:
    ld ra, 0(sp)
    ld gp, 8(sp)
    ld tp, 16(sp)
    ld s0, 24(sp)
    ld s1, 32(sp)
    ld s2, 40(sp)
    ld s3, 48(sp)
    ld s4, 56(sp)
    ld s5, 64(sp)
    ld s6, 72(sp)
    ld s7, 80(sp)
    ld s8, 88(sp)
    ld s9, 96(sp)
    ld s10, 104(sp)
    ld s11, 112(sp)
    addi sp, sp, 128
    ret
