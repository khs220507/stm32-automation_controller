#include "protocol.h"

#include "timebase.h"
#include "uart2.h"

#define PROTOCOL_MAX_CONTENT_LENGTH 62U
#define PROTOCOL_INTER_BYTE_TIMEOUT_US 50000U

static char command_buffer[PROTOCOL_MAX_CONTENT_LENGTH + 1U];
static uint8_t command_length;
static uint8_t carriage_return_received;
static uint8_t discard_until_line_end;
static uint32_t last_byte_time_us;

static void protocol_reset_receiver(void);
static uint8_t protocol_text_equals(const char *left, const char *right);
static protocol_command_t protocol_parse_command(void);

void protocol_init(void)
{
    protocol_reset_receiver();
}

protocol_command_t protocol_poll_command(void)
{
    uint8_t received_byte;

    if (((command_length != 0U) || (carriage_return_received != 0U) ||
         (discard_until_line_end != 0U)) &&
        (timebase_elapsed_us(last_byte_time_us) >= PROTOCOL_INTER_BYTE_TIMEOUT_US))
    {
        protocol_reset_receiver();
    }

    while (uart2_try_read_byte(&received_byte) != 0U)
    {
        last_byte_time_us = timebase_now_us();

        if (discard_until_line_end != 0U)
        {
            if ((carriage_return_received != 0U) && (received_byte == (uint8_t)'\n'))
            {
                protocol_reset_receiver();
            }
            else
            {
                carriage_return_received = (received_byte == (uint8_t)'\r') ? 1U : 0U;
            }
            continue;
        }

        if (carriage_return_received != 0U)
        {
            if (received_byte == (uint8_t)'\n')
            {
                protocol_command_t command = protocol_parse_command();
                protocol_reset_receiver();
                return command;
            }

            discard_until_line_end = 1U;
            carriage_return_received = (received_byte == (uint8_t)'\r') ? 1U : 0U;
            continue;
        }

        if (received_byte == (uint8_t)'\r')
        {
            carriage_return_received = 1U;
        }
        else if ((received_byte < 0x20U) || (received_byte > 0x7EU) ||
                 (command_length >= PROTOCOL_MAX_CONTENT_LENGTH))
        {
            discard_until_line_end = 1U;
            carriage_return_received = 0U;
        }
        else
        {
            command_buffer[command_length] = (char)received_byte;
            command_length++;
        }
    }

    return PROTOCOL_COMMAND_NONE;
}

void protocol_send_ready(void)
{
    uart2_write_text("READY\r\n");
}

void protocol_send_state(const char *state_name)
{
    uart2_write_text(state_name);
    uart2_write_text("\r\n");
}

void protocol_send_status(const char *state_name)
{
    uart2_write_text("OK,GET_STATUS,");
    uart2_write_text(state_name);
    uart2_write_text(",0\r\n");
}

void protocol_send_hcsr04_ok(uint32_t distance_cm, uint32_t pulse_us)
{
    uart2_write_text("HCSR04 OK DIST_CM=");
    uart2_write_u32(distance_cm);
    uart2_write_text(" PULSE_US=");
    uart2_write_u32(pulse_us);
    uart2_write_text("\r\n");
}

void protocol_send_hcsr04_out_of_range(uint32_t pulse_us)
{
    uart2_write_text("HCSR04 OUT_OF_RANGE PULSE_US=");
    uart2_write_u32(pulse_us);
    uart2_write_text("\r\n");
}

void protocol_send_hcsr04_timeout(void)
{
    uart2_write_text("HCSR04 TIMEOUT\r\n");
}

static void protocol_reset_receiver(void)
{
    command_length = 0U;
    carriage_return_received = 0U;
    discard_until_line_end = 0U;
    last_byte_time_us = timebase_now_us();
}

static uint8_t protocol_text_equals(const char *left, const char *right)
{
    while ((*left != '\0') && (*right != '\0'))
    {
        if (*left != *right)
        {
            return 0U;
        }
        left++;
        right++;
    }

    return ((*left == '\0') && (*right == '\0')) ? 1U : 0U;
}

static protocol_command_t protocol_parse_command(void)
{
    command_buffer[command_length] = '\0';

    if (protocol_text_equals(command_buffer, "START") != 0U)
    {
        return PROTOCOL_COMMAND_START;
    }
    if (protocol_text_equals(command_buffer, "STOP") != 0U)
    {
        return PROTOCOL_COMMAND_STOP;
    }
    if (protocol_text_equals(command_buffer, "GET_STATUS") != 0U)
    {
        return PROTOCOL_COMMAND_GET_STATUS;
    }

    return PROTOCOL_COMMAND_UNKNOWN;
}
