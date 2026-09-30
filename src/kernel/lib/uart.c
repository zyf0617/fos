/* low-level driver routines for 16550a UART. */

#include "mod.h"

// from printf.c 终止输出的标志
extern volatile int panicked;

// uart 初始化
void uart_init(void)
{
  	// 关闭中断
	WriteReg(IER, 0x00);

	// 进入设置比特率的模式
	WriteReg(LCR, LCR_BAUD_LATCH);

	// 设置比特率的低位和高位，最终设置为38.4K
	WriteReg(0, 0x03);
  	WriteReg(1, 0x00);

	// 设置传输字节长度为8bit,不校验
	WriteReg(LCR, LCR_EIGHT_BITS);

	// 清零和使能FIFO模式
	WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

	// 当前是同步输出，只需要RX中断。持续开启TX空中断会造成中断风暴。
	WriteReg(IER, IER_RX_ENABLE);
}

// 单个字符输出
void uart_putc_sync(int c)
{
	// 关闭中断
	push_off();

	// 如果错误发生则卡住
	while (panicked)
		;

	// 等待TX队列进入idle状态
	while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
		;

	// 输出
	WriteReg(THR, c);

	// 开启中断
	pop_off();
}

// 单个字符输入
// 失败返回-1
int uart_getc_sync(void)
{
	if (ReadReg(LSR) & 0x01)
		return ReadReg(RHR);
	else
		return -1;
}

// 中断处理(键盘输入->屏幕输出)
void uart_intr(void)
{
	while (1)
	{
		int c = uart_getc_sync();
		if (c == -1)
			break;

		if (c == '\r' || c == '\n')
		{
			// 将不同终端的行结束符统一回显为CRLF。
			uart_putc_sync('\r');
			uart_putc_sync('\n');
		}
		else if (c == '\b' || c == 0x7f)
		{
			// 先退一格、擦除原字符，再退回编辑位置。
			uart_putc_sync('\b');
			uart_putc_sync(' ');
			uart_putc_sync('\b');
		}
		else
		{
			uart_putc_sync(c);
		}
	}
}
