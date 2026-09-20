#define UART0 0x10000000L
#define UART_THR 0
#define UART_LSR 5
#define UART_LSR_TX_IDLE (1 << 5)

static volatile unsigned int uart_lock;

void uart_lock_acquire(void) {
    unsigned int old;
    do {
        asm volatile("amoswap.w.aq %0, %1, (%2)"
                     : "=r"(old)
                     : "r"(1u), "r"(&uart_lock)
                     : "memory");
    } while (old != 0);
}

void uart_lock_release(void) {
    asm volatile("amoswap.w.rl zero, zero, (%0)"
                 :
                 : "r"(&uart_lock)
                 : "memory");
}

static inline unsigned char uart_read_reg(int reg) {
    return *(volatile unsigned char *)(UART0 + reg);
}

static inline void uart_write_reg(int reg, unsigned char value) {
    *(volatile unsigned char *)(UART0 + reg) = value;
}

static void uart_putc_raw(char c) {
    while ((uart_read_reg(UART_LSR) & UART_LSR_TX_IDLE) == 0) {
        ;
    }
    uart_write_reg(UART_THR, c);
}

void uart_putc(char c) {
    if (c == '\n') {
        uart_putc_raw('\r');
    }
    uart_putc_raw(c);
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}
