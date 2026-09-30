#pragma once

/* PLIC */
void plic_init(void);
void plic_inithart(void);
int plic_claim(void);
void plic_complete(int irq);

/* Timer */
void timer_init(void);
void timer_create(void);
void timer_update(void);
uint64 timer_get_ticks(void);

/* Kernel trap */
void trap_kernel_init(void);
void trap_kernel_inithart(void);
void trap_kernel_handler(void);
void external_interrupt_handler(void);
void timer_interrupt_handler(void);
