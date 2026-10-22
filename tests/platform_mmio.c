/* Linux宿主探针：执行实际uart.c/plic.c；不能代替JH7110硬件验收。 */
#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

volatile int panicked;
unsigned long long boot_hartid;
static int hart, depth;
int mycpuid(void) { return hart; }
void push_off(void) { depth++; }
void pop_off(void) { if (--depth < 0) _exit(2); }
static void report(int fd, const char *s)
{
    if (write(fd, s, strlen(s)) < 0) _exit(2);
}
void assert(int condition, const char *message)
{
    if (!condition) { report(2, message); report(2, "\n"); _exit(1); }
}
void uart_init(void);
void uart_enable_rx_interrupt(void);
void uart_putc_sync(int c);
int uart_getc_sync(void);
void plic_init(void);
void plic_inithart(void);
int plic_claim(void);
void plic_complete(int irq);
#define WORD(address) (*(volatile uint32_t *)(uintptr_t)(address))
#ifdef VISIONFIVE2
#define IRQ 32
#define FIRST 1
#define SLOTS 5
#define CONTEXT(h) (2 * (h)) /* DT: CPU0 M only, then each U74 M/S pair. */
#define UART_REG(n) WORD(0x10000000ul + 4 * (n))
#else
#define IRQ 10
#define FIRST 0
#define SLOTS 2
#define CONTEXT(h) (2 * (h) + 1)
#define UART_REG(n) (*(volatile uint8_t *)(uintptr_t)(0x10000000ul + (n)))
#endif
int main(void)
{
    assert(mmap((void *)0xc000000, 0x4000000, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == (void *)0xc000000, "map PLIC failed");
    assert(mmap((void *)0x10000000, 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == (void *)0x10000000, "map UART failed");
    plic_init();
    assert(WORD(0xc000000 + 4 * IRQ) == 1, "UART priority address");
    for (hart = FIRST; hart < SLOTS; hart++)
    {
        boot_hartid = hart;
        uintptr_t enable = 0xc002000ul + 0x80 * CONTEXT(hart);
        uintptr_t priority = 0xc200000ul + 0x1000 * CONTEXT(hart);
        for (int i = 0; i < 5; i++) WORD(enable + 4 * i) = 0xdeadbeef;
        WORD(enable - 4) = WORD(enable + 20) = 0x12345678;
        WORD(priority - 4) = WORD(priority + 8) = 0x23456789;
        WORD(priority) = 0xdeadbeef;
        plic_inithart();
        for (int i = 0; i < 5; i++)
            assert(WORD(enable + 4 * i) == (i == IRQ / 32 ? 1u << (IRQ % 32) : 0), "PLIC enable address/width");
        assert(WORD(enable - 4) == 0x12345678 && WORD(enable + 20) == 0x12345678 &&
               WORD(priority - 4) == 0x23456789 && WORD(priority + 8) == 0x23456789 &&
               WORD(priority) == 0, "PLIC neighboring context modified");
        WORD(priority + 4) = IRQ;
        assert(plic_claim() == IRQ, "PLIC claim context");
        plic_complete(IRQ + 1);
        assert(WORD(priority + 4) == IRQ + 1, "PLIC complete context");
        boot_hartid = SLOTS; /* 辅助hart必须禁用UART。 */
        plic_inithart();
        assert(WORD(enable + 4 * (IRQ / 32)) == 0, "UART routed to auxiliary hart");
    }
    memset((void *)0x10000000, 0xa5, 4096);
    uart_init();
    assert(UART_REG(1) == 0 && UART_REG(2) == 7, "UART init/RX timing");
#ifdef VISIONFIVE2
    assert(WORD(0x1000000c) == 0xa5a5a5a5, "VF2 overwrote firmware LCR");
#endif
    uart_enable_rx_interrupt();
    assert(UART_REG(1) == 1, "UART RX enable");
    UART_REG(5) = 0x20;
    uart_putc_sync('Z');
    assert(UART_REG(0) == 'Z' && depth == 0, "UART transmit/interrupt nesting");
    UART_REG(5) = 0x21;
    UART_REG(0) = 0xab;
    assert(uart_getc_sync() == 0xab, "UART receive");
    UART_REG(5) = 0;
    assert(uart_getc_sync() == -1, "UART empty receive");
    report(1, "platform MMIO PASS\n");
    return 0;
}
