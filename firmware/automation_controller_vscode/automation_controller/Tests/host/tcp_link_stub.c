#include "tcp_link.h"
#include "uart2.h"
volatile tcp_status_t tcp_link_status;
void tcp_link_init(void) { uart2_init(); }
void tcp_link_poll(void) { }
bool tcp_link_connected(void) { return true; }
uint32_t tcp_link_session(void) { return 0U; }
uint8_t tcp_link_try_read_byte(uint8_t *value) { return uart2_try_read_byte(value); }
void tcp_link_write_text(const char *text) { uart2_write_text(text); }
void tcp_link_write_u32(uint32_t value) { uart2_write_u32(value); }
void tcp_link_abort(void) { }
