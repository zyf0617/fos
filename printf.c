#include "printf.h"
#include "uart.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

static void print_unsigned(unsigned long value, int base) {
    static const char digits[] = "0123456789abcdef";
    char buffer[sizeof(unsigned long) * 2];
    int i = 0;

    if (value == 0) {
        uart_putc('0');
        return;
    }
    while (value != 0) {
        buffer[i++] = digits[value % base];
        value /= base;
    }
    while (i > 0)
        uart_putc(buffer[--i]);
}

static void print_signed(long value) {
    if (value < 0) {
        uart_putc('-');
        print_unsigned((unsigned long)(-(value + 1)) + 1, 10);
    } else {
        print_unsigned((unsigned long)value, 10);
    }
}

void printf(const char *fmt, ...) {
    va_list ap;
    uart_lock_acquire();
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            uart_putc(*fmt);
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 'c':
            uart_putc((char)va_arg(ap, int));
            break;
        case 'd':
            print_signed(va_arg(ap, int));
            break;
        case 'x':
            print_unsigned(va_arg(ap, unsigned int), 16);
            break;
        case 'p':
            uart_puts("0x");
            print_unsigned(va_arg(ap, unsigned long), 16);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == 0)
                s = "(null)";
            uart_puts(s);
            break;
        }
        case '%':
            uart_putc('%');
            break;
        default:
            uart_putc('%');
            uart_putc(*fmt);
            break;
        }
    }
    va_end(ap);
    uart_lock_release();
}
