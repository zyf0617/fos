#include "uart.h"
#include "printf.h"

void main(void) {
    kprintf("Hello from lab-1 main!\n");
    kprintf("char=%c decimal=%d hex=%x ptr=%p string=%s percent=%%\n",
            'A', -42, 0x2a, (unsigned long)main, "ok");

    while (1) {
        ;
    }
}
