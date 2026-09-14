#include "protocol.h"

#include <string.h>

#include "timebase.h"
#include "tcp_link.h"
#include "uart1.h"
#include <stdbool.h>

#define PROTOCOL_MAX_CONTENT_LENGTH 62U


typedef struct
{
    char command_buffer[PROTOCOL_MAX_CONTENT_LENGTH + 1U];
    uint8_t command_length;
    uint8_t carriage_return_received;
    uint8_t discard_until_line_end;
} protocol_receiver_t;

static protocol_receiver_t tcp_receiver;
static protocol_receiver_t rs485_receiver;
static protocol_receiver_t *receiver;
static bool response_rs485;
static bool prefer_rs485;

static void protocol_reset_receiver(void);
static protocol_command_t protocol_parse_command(void);
static void protocol_write_text(const char *text);
static void protocol_write_u32(uint32_t value);

void protocol_init(void)
{
    memset(&tcp_receiver, 0, sizeof(tcp_receiver));
    memset(&rs485_receiver, 0, sizeof(rs485_receiver));
    receiver = &tcp_receiver;
    response_rs485 = false;
    prefer_rs485 = false;
}

void protocol_reset_tcp(void)
{
    memset(&tcp_receiver, 0, sizeof(tcp_receiver));
}

bool protocol_is_rs485(void)
{
    return response_rs485;
}

static protocol_command_t protocol_poll_source(bool rs485)
{
    receiver = rs485 ? &rs485_receiver : &tcp_receiver;
    uint8_t received_byte;

    while (true)
    {
        uint8_t available = rs485 ? uart1_try_read_byte(&received_byte)
            : tcp_link_try_read_byte(&received_byte);
        if (available == 0U)
            break;

        if (rs485 && received_byte == (uint8_t)'\n' &&
            (receiver->carriage_return_received == 0U || receiver->discard_until_line_end != 0U))
        {
            protocol_reset_receiver();
            continue;
        }

        if (receiver->discard_until_line_end != 0U)
        {
            if ((receiver->carriage_return_received != 0U) && (received_byte == (uint8_t)'\n'))
            {
                protocol_reset_receiver();
            }
            else
            {
                receiver->carriage_return_received = (received_byte == (uint8_t)'\r') ? 1U : 0U;
            }
            continue;
        }

        if (receiver->carriage_return_received != 0U)
        {
            if (received_byte == (uint8_t)'\n')
            {
                protocol_command_t command = protocol_parse_command();
                protocol_reset_receiver();
                return command;
            }

            receiver->discard_until_line_end = 1U;
            receiver->carriage_return_received = (received_byte == (uint8_t)'\r') ? 1U : 0U;
            continue;
        }

        if (received_byte == (uint8_t)'\r')
        {
            receiver->carriage_return_received = 1U;
        }
        else if ((received_byte < 0x20U) || (received_byte > 0x7EU) ||
                 (receiver->command_length >= PROTOCOL_MAX_CONTENT_LENGTH))
        {
            receiver->discard_until_line_end = 1U;
            receiver->carriage_return_received = 0U;
        }
        else
        {
            receiver->command_buffer[receiver->command_length] = (char)received_byte;
            receiver->command_length++;
        }
    }

    return PROTOCOL_COMMAND_NONE;
}

protocol_command_t protocol_poll_command(void)
{
    for (uint8_t i = 0U; i < 2U; i++)
    {
        bool rs485 = (i == 0U) ? prefer_rs485 : (prefer_rs485 == false);
        protocol_command_t command = protocol_poll_source(rs485);
        if (command != PROTOCOL_COMMAND_NONE)
        {
            response_rs485 = rs485;
            prefer_rs485 = (rs485 == false);
            return command;
        }
    }
    response_rs485 = false;
    return PROTOCOL_COMMAND_NONE;
}

static void protocol_write_text(const char *text)
{
    if (response_rs485)
        uart1_write_text(text);
    else
        tcp_link_write_text(text);
}

static void protocol_write_u32(uint32_t value)
{
    char digits[11];
    uint8_t position = 10U;
    digits[position] = '\0';
    do
    {
        digits[--position] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    protocol_write_text(&digits[position]);
}

void protocol_send_ready(void)
{
    protocol_write_text("READY\r\n");
}

void protocol_send_pong(void)
{
    protocol_write_text("OK,PING,PONG\r\n");
}

void protocol_send_error(const char *command, const char *error_code)
{
    protocol_write_text("ERR,");
    protocol_write_text(command);
    protocol_write_text(",");
    protocol_write_text(error_code);
    protocol_write_text("\r\n");
}

void protocol_send_hcsr04_check(const char *status, uint32_t distance_cm, uint32_t pulse_us)
{
    protocol_write_text("OK,CHECK_HCSR04,");
    protocol_write_text(status);
    protocol_write_text(",");
    protocol_write_u32(distance_cm);
    protocol_write_text(",");
    protocol_write_u32(pulse_us);
    protocol_write_text("\r\n");
}

void protocol_send_state(const char *state_name)
{
    protocol_write_text(state_name);
    protocol_write_text("\r\n");
}

void protocol_send_status(const char *state_name)
{
    protocol_write_text("OK,GET_STATUS,");
    protocol_write_text(state_name);
    protocol_write_text(",0\r\n");
}

void protocol_send_hcsr04_ok(uint32_t distance_cm, uint32_t pulse_us)
{
    protocol_write_text("HCSR04 OK DIST_CM=");
    protocol_write_u32(distance_cm);
    protocol_write_text(" PULSE_US=");
    protocol_write_u32(pulse_us);
    protocol_write_text("\r\n");
}

void protocol_send_mpu6050_id(uint8_t identity)
{
    /* OK는 레지스터 읽기 성공이며, MPU6050 식별 일치는 UI에서 별도 판정한다. */
    protocol_write_text("OK,CHECK_MPU6050,");
    protocol_write_u32(identity);
    protocol_write_text("\r\n");
}

void protocol_send_mpu6050_wake(uint8_t before, uint8_t after)
{
    protocol_write_text("OK,WAKE_MPU6050,");
    protocol_write_u32(before);
    protocol_write_text(",");
    protocol_write_u32(after);
    protocol_write_text("\r\n");
}

void protocol_send_w5500_version(uint8_t version)
{
    protocol_write_text("OK,CHECK_W5500,");
    protocol_write_u32(version);
    protocol_write_text("\r\n");
}

void protocol_send_hcsr04_out_of_range(uint32_t pulse_us)
{
    protocol_write_text("HCSR04 OUT_OF_RANGE PULSE_US=");
    protocol_write_u32(pulse_us);
    protocol_write_text("\r\n");
}

void protocol_send_hcsr04_timeout(void)
{
    protocol_write_text("HCSR04 TIMEOUT\r\n");
}

static void protocol_reset_receiver(void)
{
    receiver->command_length = 0U;
    receiver->carriage_return_received = 0U;
    receiver->discard_until_line_end = 0U;

}

static protocol_command_t protocol_parse_command(void)
{
    receiver->command_buffer[receiver->command_length] = '\0';

    if (strcmp(receiver->command_buffer, "CONFIG_ACCEL") == 0)
        return PROTOCOL_COMMAND_CONFIG_ACCEL;
    if (strcmp(receiver->command_buffer, "READ_ACCEL") == 0)
        return PROTOCOL_COMMAND_READ_ACCEL;
    if (strcmp(receiver->command_buffer, "CHECK_W5500") == 0)
        return PROTOCOL_COMMAND_CHECK_W5500;

    /* 표준 C strcmp는 두 문자열이 같으면 0을 반환한다. */
    if (strcmp(receiver->command_buffer, "PING") == 0)
        return PROTOCOL_COMMAND_PING;
    if (strcmp(receiver->command_buffer, "CHECK_HCSR04") == 0)
        return PROTOCOL_COMMAND_CHECK_HCSR04;

    if (strcmp(receiver->command_buffer, "START") == 0)
    {
        return PROTOCOL_COMMAND_START;
    }
    if (strcmp(receiver->command_buffer, "STOP") == 0)
    {
        return PROTOCOL_COMMAND_STOP;
    }
    if (strcmp(receiver->command_buffer, "GET_STATUS") == 0)
    {
        return PROTOCOL_COMMAND_GET_STATUS;
    }
    if (strcmp(receiver->command_buffer, "CHECK_MPU6050") == 0)
    {
        return PROTOCOL_COMMAND_CHECK_MPU6050;
    }
    if (strcmp(receiver->command_buffer, "WAKE_MPU6050") == 0)
        return PROTOCOL_COMMAND_WAKE_MPU6050;

    return PROTOCOL_COMMAND_UNKNOWN;
}

void protocol_send_accel_configured(void)
{
    protocol_write_text("OK,CONFIG_ACCEL,2G\r\n");
}

static void protocol_send_i16(int16_t value)
{
    /* -32768도 int32_t에서 양수로 바꾸므로 오버플로하지 않는다. */
    int32_t number = value;
    if (number < 0)
    {
        protocol_write_text("-");
        number = -number;
    }
    protocol_write_u32((uint32_t)number);
}

void protocol_send_accel(int16_t x, int16_t y, int16_t z)
{
    /* ±2g 원시값을 전송한다. PC에서 16384 LSB/g로 환산한다. */
    protocol_write_text("OK,READ_ACCEL,");
    protocol_send_i16(x);
    protocol_write_text(",");
    protocol_send_i16(y);
    protocol_write_text(",");
    protocol_send_i16(z);
    protocol_write_text("\r\n");
}
