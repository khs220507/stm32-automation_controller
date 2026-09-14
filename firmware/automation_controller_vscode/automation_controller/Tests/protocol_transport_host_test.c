#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"

static const char *tcp_input = "";
static const char *serial_input = "";
static char tcp_output[256];
static char serial_output[256];

uint8_t tcp_link_try_read_byte(uint8_t *value)
{
    if (*tcp_input == '\0')
        return 0U;
    *value = (uint8_t)*tcp_input++;
    return 1U;
}

uint8_t uart1_try_read_byte(uint8_t *value)
{
    if (*serial_input == '\0')
        return 0U;
    *value = (uint8_t)*serial_input++;
    return 1U;
}

void tcp_link_write_text(const char *text)
{
    strcat(tcp_output, text);
}

void uart1_write_text(const char *text)
{
    strcat(serial_output, text);
}

int main(void)
{
    protocol_init();
    tcp_input = "CHECK_";
    assert(protocol_poll_command() == PROTOCOL_COMMAND_NONE);
    serial_input = "PING\r\n";
    assert(protocol_poll_command() == PROTOCOL_COMMAND_PING);
    assert(protocol_is_rs485());
    protocol_send_pong();
    assert(strcmp(serial_output, "OK,PING,PONG\r\n") == 0);
    assert(tcp_output[0] == '\0');
    tcp_input = "W5500\r\n";
    assert(protocol_poll_command() == PROTOCOL_COMMAND_CHECK_W5500);
    assert(protocol_is_rs485() == false);
    protocol_send_w5500_version(4U);
    assert(strcmp(tcp_output, "OK,CHECK_W5500,4\r\n") == 0);
    serial_output[0] = '\0';
    serial_input = "bad\nPING\rX\nREAD_ACCEL\r\n";
    assert(protocol_poll_command() == PROTOCOL_COMMAND_READ_ACCEL);
    protocol_send_accel(-32768, 0, 32767);
    assert(strcmp(serial_output, "OK,READ_ACCEL,-32768,0,32767\r\n") == 0);
    tcp_input = "PI";
    assert(protocol_poll_command() == PROTOCOL_COMMAND_NONE);
    protocol_reset_tcp();
    tcp_input = "PING\r\n";
    serial_input = "GET_STATUS\r\n";
    assert(protocol_poll_command() == PROTOCOL_COMMAND_PING);
    assert(protocol_is_rs485() == false);
    assert(protocol_poll_command() == PROTOCOL_COMMAND_GET_STATUS);
    assert(protocol_is_rs485());
    assert(protocol_poll_command() == PROTOCOL_COMMAND_NONE);
    assert(protocol_is_rs485() == false);
    puts("Protocol transport model passed: isolated partial frames, correct response routing, malformed serial recovery, signed values, session reset and fairness.");
}
