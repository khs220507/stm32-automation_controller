#ifndef UART2_H
#define UART2_H

#include <stdint.h>

void uart2_init(void);
uint8_t uart2_try_read_byte(uint8_t *received_byte);
void uart2_write_byte(uint8_t byte);
void uart2_write_text(const char *text);
void uart2_write_u32(uint32_t value);

#endif
