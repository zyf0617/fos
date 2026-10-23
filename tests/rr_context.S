.section .text
.global rr_context
rr_context:
    addi sp, sp, -160
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
    sd a1, 128(sp)
    rdtime t6
    add t6, t6, a0
    sd t6, 136(sp)
    sd sp, 144(sp)
    addi ra, a1, 1
    addi gp, a1, 2
    addi tp, a1, 3
    addi s0, a1, 4
    addi s1, a1, 5
    addi s2, a1, 6
    addi s3, a1, 7
    addi s4, a1, 8
    addi s5, a1, 9
    addi s6, a1, 10
    addi s7, a1, 11
    addi s8, a1, 12
    addi s9, a1, 13
    addi s10, a1, 14
    addi s11, a1, 15
    addi t0, a1, 16
    addi t1, a1, 17
    addi t2, a1, 18
    addi t3, a1, 19
    addi t4, a1, 20
    addi t5, a1, 21
    addi a2, a1, 23
    addi a3, a1, 24
    addi a4, a1, 25
    addi a5, a1, 26
    addi a6, a1, 27
    addi a7, a1, 28
    addi a1, a1, 22
rr_spin:
    rdtime a0
    ld t6, 136(sp)
    bltu a0, t6, rr_spin
    ld a0, 144(sp)
    bne sp, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 1
    bne ra, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 2
    bne gp, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 3
    bne tp, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 4
    bne s0, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 5
    bne s1, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 6
    bne s2, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 7
    bne s3, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 8
    bne s4, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 9
    bne s5, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 10
    bne s6, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 11
    bne s7, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 12
    bne s8, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 13
    bne s9, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 14
    bne s10, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 15
    bne s11, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 16
    bne t0, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 17
    bne t1, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 18
    bne t2, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 19
    bne t3, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 20
    bne t4, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 21
    bne t5, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 22
    bne a1, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 23
    bne a2, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 24
    bne a3, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 25
    bne a4, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 26
    bne a5, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 27
    bne a6, a0, rr_failed
    ld a0, 128(sp)
    addi a0, a0, 28
    bne a7, a0, rr_failed
    li a0, 0
    j rr_restore
rr_failed:
    li a0, -1
rr_restore:
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
    addi sp, sp, 160
    ret
