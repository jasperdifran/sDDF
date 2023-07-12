#include <syscall_uart.h>
#include <stdint.h>

#define UARTSTAT 0x98
#define RECEIVE 0x0
#define TRANSMIT 0x40
#define STAT_TDRE (1 << 14)
#define STAT_RDRF (1 << 0)

uint64_t uart_base;
#define UART_REG(x) ((volatile uint32_t *)(uart_base + (x)))

int imx_getc(void)
{
    /* Wait until received. */
    while (!(*UART_REG(UARTSTAT) & STAT_RDRF))
    {
    }
    int val = *UART_REG(RECEIVE);
    // imx_putc((uint8_t)val);
    return val;
}

void imx_putc(char ch)
{
    while (!(*UART_REG(UARTSTAT) & STAT_TDRE))
    {
    }
    *UART_REG(TRANSMIT) = ch;
}

void imx8mm_output_strn(const char *str, size_t len)
{
    while (len--)
    {
        imx_putc(*str++);
    }
}