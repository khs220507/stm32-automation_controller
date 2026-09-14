#include "tcp_link.h"
#include "uart2.h"
#include "uart_diag.h"
void uart_diag_init(void) { }
void uart_diag_poll(void) { }
volatile tcp_status_t tcp_link_status;
void tcp_link_init(void) { uart2_init(); }
void tcp_link_poll(void) { }
bool host_tcp_connected = true;
uint32_t host_tcp_session;
bool tcp_link_connected(void) { return host_tcp_connected; }
uint32_t tcp_link_session(void) { return host_tcp_session; }
uint8_t tcp_link_try_read_byte(uint8_t *value) { return uart2_try_read_byte(value); }
void tcp_link_write_text(const char *text) { uart2_write_text(text); }
void tcp_link_write_u32(uint32_t value) { uart2_write_u32(value); }
void tcp_link_abort(void) { }

void uart1_init(void)
{
}
uint8_t uart1_try_read_byte(uint8_t *value)
{
    (void)value;
    return 0U;
}
void uart1_write_text(const char *text)
{
    (void)text;
}
