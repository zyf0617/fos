typedef unsigned long uint64;

extern void main(void);

static inline uint64 r_mstatus(void) {
    uint64 x;
    asm volatile("csrr %0, mstatus" : "=r"(x));
    return x;
}

static inline void w_mstatus(uint64 x) {
    asm volatile("csrw mstatus, %0" : : "r"(x));
}

static inline void w_mepc(uint64 x) {
    asm volatile("csrw mepc, %0" : : "r"(x));
}

static inline void w_satp(uint64 x) {
    asm volatile("csrw satp, %0" : : "r"(x));
}

static inline void w_medeleg(uint64 x) {
    asm volatile("csrw medeleg, %0" : : "r"(x));
}

static inline void w_mideleg(uint64 x) {
    asm volatile("csrw mideleg, %0" : : "r"(x));
}

static inline void w_pmpaddr0(uint64 x) {
    asm volatile("csrw pmpaddr0, %0" : : "r"(x));
}

static inline void w_pmpcfg0(uint64 x) {
    asm volatile("csrw pmpcfg0, %0" : : "r"(x));
}

void start(void) {
    unsigned long x = r_mstatus();

    // 设置 mret 之后进入 S-mode
    x &= ~(3L << 11);
    x |=  (1L << 11);
    w_mstatus(x);

    // mret 之后跳转到 main
    w_mepc((uint64)main);

    // 关闭分页
    w_satp(0);

    // 将异常和中断委托给 S-mode
    w_medeleg(0xffff);
    w_mideleg(0xffff);

    // 允许 S-mode 访问全部物理内存
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);

    asm volatile("mret");
}
