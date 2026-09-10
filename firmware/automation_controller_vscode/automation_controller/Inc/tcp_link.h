#ifndef TCP_LINK_H
#define TCP_LINK_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    TCP_WAIT_MODULE,
    TCP_LISTENING,
    TCP_CONNECTED,
    TCP_LINK_DOWN,
    TCP_IO_FAULT
} tcp_status_t;

extern volatile tcp_status_t tcp_link_status;

void tcp_link_init(void);
void tcp_link_poll(void);
bool tcp_link_connected(void);
uint32_t tcp_link_session(void);
uint8_t tcp_link_try_read_byte(uint8_t *value);
void tcp_link_write_text(const char *text);
void tcp_link_write_u32(uint32_t value);
void tcp_link_abort(void);

#endif
