#include "../src/kernel/mem/type.h"
#include "../src/kernel/trap/type.h"
#include "../src/kernel/lib/type.h"
_Static_assert(IER_RX_ENABLE == 1 && IER_TX_ENABLE == 2, "16550 RX/TX bits");
_Static_assert(PHYS_MEM_END == KERNEL_BASE + 128ul * 1024 * 1024, "managed window");
#ifdef VISIONFIVE2
_Static_assert(FIRST_HART == 1 && NCPU == 5, "U74 physical slots");
_Static_assert(TIMER_INTERVAL == 400000 && KERNEL_BASE == 0x40200000, "VF2 timer/load");
_Static_assert(PTE_MACHINE == (PTE_A | PTE_D), "VF2 explicit AD");
_Static_assert(PLIC_SENABLE(1) == 0x0c002100 && PLIC_SCLAIM(1) == 0x0c202004, "VF2 S context1");
#else
_Static_assert(FIRST_HART == 0 && NCPU == 2, "QEMU slots");
_Static_assert(TIMER_INTERVAL == 1000000 && KERNEL_BASE == 0x80200000, "QEMU timer/load");
_Static_assert(PTE_MACHINE == 0, "QEMU hardware AD");
_Static_assert(PLIC_SENABLE(0) == 0x0c002080 && PLIC_SCLAIM(0) == 0x0c201004, "QEMU S context0");
#endif
int main(void) { return 0; }
