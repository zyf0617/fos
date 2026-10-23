#include "../src/user/sys.h"
#define SAMPLES 31
#define QUERY_ITERATIONS 1024
#define MAP_ITERATIONS 128
#define PAGE 4096ul

static unsigned long now(void)
{
    unsigned long ticks;
    asm volatile("csrr %0, time" : "=r"(ticks) : : "memory");
    return ticks;
}

static char *append(char *out, const char *text)
{
    while (*text) *out++ = *text++;
    return out;
}

static char *decimal(char *out, unsigned long value)
{
    char reversed[24];
    int count = 0;
    do { reversed[count++] = '0' + value % 10; value /= 10; } while (value);
    while (count) *out++ = reversed[--count];
    return out;
}

static void report(const char *workload, unsigned long sample,
                   unsigned long ticks, unsigned long iterations)
{
    char line[128], *out = append(line, "aegis bench workload=");
    out = append(out, workload);
    out = append(out, " sample="); out = decimal(out, sample);
    out = append(out, " ticks="); out = decimal(out, ticks);
    out = append(out, " iterations="); out = decimal(out, iterations);
    *out++ = '\n'; *out = 0;
    syscall(SYS_printf, line);
}

static void failed(void)
{
    syscall(SYS_printf, "aegis BENCH FAILED\n");
    asm volatile("ld zero, 16(zero)" ::: "memory");
    while (1) ;
}

__attribute__((section(".text.entry"))) int main(void)
{
    // 每批测量前只预热，不在计时区间内进行串口输出。
    for (int sample = 0; sample < SAMPLES; sample++)
    {
        for (int i = 0; i < 128; i++) syscall(SYS_brk, 0);
        unsigned long sum = 0, start = now();
        for (int i = 0; i < QUERY_ITERATIONS; i++) sum += syscall(SYS_brk, 0);
        unsigned long elapsed = now() - start;
        if (sum != 2 * PAGE * QUERY_ITERATIONS || !elapsed) failed();
        report("query", sample, elapsed, QUERY_ITERATIONS);
    }
    for (int sample = 0; sample < SAMPLES; sample++)
    {
        for (int i = 0; i < 16; i++)
        {
            unsigned long address = syscall(SYS_mmap, 0, PAGE);
            if (!address || syscall(SYS_munmap, address, PAGE)) failed();
        }
        int good = 1;
        unsigned long start = now();
        for (int i = 0; i < MAP_ITERATIONS; i++)
        {
            unsigned long address = syscall(SYS_mmap, 0, PAGE);
            long result = syscall(SYS_munmap, address, PAGE);
            good &= address != 0 && result == 0;
        }
        unsigned long elapsed = now() - start;
        if (!good || !elapsed) failed();
        report("map-cycle", sample, elapsed, MAP_ITERATIONS);
    }
    syscall(SYS_printf, "aegis BENCH PASS\n");
    asm volatile("ld zero, 0(zero)" ::: "memory");
    while (1) ;
}
