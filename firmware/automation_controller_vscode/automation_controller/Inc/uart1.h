#ifndef UART1_H
#define UART1_H
#include <stdint.h>

/* 부팅 시 GPIOB와 USART1 클록을 활성화한다. */
void uart1_enable_clocks(void);
void uart1_init(void);
uint8_t uart1_try_read_byte(uint8_t *value);
void uart1_write_text(const char *text);

#endif
