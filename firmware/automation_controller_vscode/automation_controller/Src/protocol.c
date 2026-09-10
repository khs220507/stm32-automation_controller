#include "protocol.h"

#include <string.h>

#include "timebase.h"
#include "tcp_link.h"

#define PROTOCOL_MAX_CONTENT_LENGTH 62U


static char command_buffer[PROTOCOL_MAX_CONTENT_LENGTH + 1U];
static uint8_t command_length;
static uint8_t carriage_return_received;
static uint8_t discard_until_line_end;


static void protocol_reset_receiver(void);
static protocol_command_t protocol_parse_command(void);

void protocol_init(void)
{
    protocol_reset_receiver();
}

protocol_command_t protocol_poll_command(void)
{
    uint8_t received_byte;

    while (tcp_link_try_read_byte(&received_byte) != 0U)
    {


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
    tcp_link_write_text("READY\r\n");
}

void protocol_send_pong(void)
{
    tcp_link_write_text("OK,PING,PONG\r\n");
}

void protocol_send_error(const char *command, const char *error_code)
{
    tcp_link_write_text("ERR,");
    tcp_link_write_text(command);
    tcp_link_write_text(",");
    tcp_link_write_text(error_code);
    tcp_link_write_text("\r\n");
}

void protocol_send_hcsr04_check(const char *status, uint32_t distance_cm, uint32_t pulse_us)
{
    tcp_link_write_text("OK,CHECK_HCSR04,");
    tcp_link_write_text(status);
    tcp_link_write_text(",");
    tcp_link_write_u32(distance_cm);
    tcp_link_write_text(",");
    tcp_link_write_u32(pulse_us);
    tcp_link_write_text("\r\n");
}

void protocol_send_state(const char *state_name)
{
    tcp_link_write_text(state_name);
    tcp_link_write_text("\r\n");
}

void protocol_send_status(const char *state_name)
{
    tcp_link_write_text("OK,GET_STATUS,");
    tcp_link_write_text(state_name);
    tcp_link_write_text(",0\r\n");
}

void protocol_send_hcsr04_ok(uint32_t distance_cm, uint32_t pulse_us)
{
    tcp_link_write_text("HCSR04 OK DIST_CM=");
    tcp_link_write_u32(distance_cm);
    tcp_link_write_text(" PULSE_US=");
    tcp_link_write_u32(pulse_us);
    tcp_link_write_text("\r\n");
}

void protocol_send_mpu6050_id(uint8_t identity)
{
    /* OK는 레지스터 읽기 성공이며, MPU6050 식별 일치는 UI에서 별도 판정한다. */
    tcp_link_write_text("OK,CHECK_MPU6050,");
    tcp_link_write_u32(identity);
    tcp_link_write_text("\r\n");
}

void protocol_send_mpu6050_wake(uint8_t before, uint8_t after)
{
    tcp_link_write_text("OK,WAKE_MPU6050,");
    tcp_link_write_u32(before);
    tcp_link_write_text(",");
    tcp_link_write_u32(after);
    tcp_link_write_text("\r\n");
}

void protocol_send_w5500_version(uint8_t version)
{
    tcp_link_write_text("OK,CHECK_W5500,");
    tcp_link_write_u32(version);
    tcp_link_write_text("\r\n");
}

void protocol_send_hcsr04_out_of_range(uint32_t pulse_us)
{
    tcp_link_write_text("HCSR04 OUT_OF_RANGE PULSE_US=");
    tcp_link_write_u32(pulse_us);
    tcp_link_write_text("\r\n");
}

void protocol_send_hcsr04_timeout(void)
{
    tcp_link_write_text("HCSR04 TIMEOUT\r\n");
}

static void protocol_reset_receiver(void)
{
    command_length = 0U;
    carriage_return_received = 0U;
    discard_until_line_end = 0U;

}

static protocol_command_t protocol_parse_command(void)
{
    command_buffer[command_length] = '\0';

    if (strcmp(command_buffer, "CONFIG_ACCEL") == 0) return PROTOCOL_COMMAND_CONFIG_ACCEL;
    if (strcmp(command_buffer, "READ_ACCEL") == 0) return PROTOCOL_COMMAND_READ_ACCEL;
    if (strcmp(command_buffer, "CHECK_W5500") == 0) return PROTOCOL_COMMAND_CHECK_W5500;

    /* 표준 C strcmp는 두 문자열이 같으면 0을 반환한다. */
    if (strcmp(command_buffer, "PING") == 0)
        return PROTOCOL_COMMAND_PING;
    if (strcmp(command_buffer, "CHECK_HCSR04") == 0)
        return PROTOCOL_COMMAND_CHECK_HCSR04;

    if (strcmp(command_buffer, "START") == 0)
    {
        return PROTOCOL_COMMAND_START;
    }
    if (strcmp(command_buffer, "STOP") == 0)
    {
        return PROTOCOL_COMMAND_STOP;
    }
    if (strcmp(command_buffer, "GET_STATUS") == 0)
    {
        return PROTOCOL_COMMAND_GET_STATUS;
    }
    if (strcmp(command_buffer, "CHECK_MPU6050") == 0)
    {
        return PROTOCOL_COMMAND_CHECK_MPU6050;
    }
    if (strcmp(command_buffer, "WAKE_MPU6050") == 0)
        return PROTOCOL_COMMAND_WAKE_MPU6050;

    return PROTOCOL_COMMAND_UNKNOWN;
}

void protocol_send_accel_configured(void)
{
    tcp_link_write_text("OK,CONFIG_ACCEL,2G\r\n");
}

static void protocol_send_i16(int16_t value)
{
    /* -32768도 int32_t에서 양수로 바꾸므로 오버플로하지 않는다. */
    int32_t number = value;
    if (number < 0) { tcp_link_write_text("-"); number = -number; }
    tcp_link_write_u32((uint32_t)number);
}

void protocol_send_accel(int16_t x, int16_t y, int16_t z)
{
    /* ±2g 원시값을 전송한다. PC에서 16384 LSB/g로 환산한다. */
    tcp_link_write_text("OK,READ_ACCEL,");
    protocol_send_i16(x);
    tcp_link_write_text(",");
    protocol_send_i16(y);
    tcp_link_write_text(",");
    protocol_send_i16(z);
    tcp_link_write_text("\r\n");
}
