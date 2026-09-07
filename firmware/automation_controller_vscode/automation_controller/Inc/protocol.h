#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

typedef enum
{
    PROTOCOL_COMMAND_NONE = 0,
    PROTOCOL_COMMAND_START,
    PROTOCOL_COMMAND_STOP,
    PROTOCOL_COMMAND_GET_STATUS,
    PROTOCOL_COMMAND_CHECK_MPU6050,
    PROTOCOL_COMMAND_PING,
    PROTOCOL_COMMAND_CHECK_HCSR04,
    PROTOCOL_COMMAND_UNKNOWN
} protocol_command_t;

void protocol_init(void);
protocol_command_t protocol_poll_command(void);
void protocol_send_ready(void);
void protocol_send_state(const char *state_name);
void protocol_send_status(const char *state_name);
void protocol_send_pong(void);
void protocol_send_error(const char *command, const char *error_code);
void protocol_send_hcsr04_check(const char *status, uint32_t distance_cm, uint32_t pulse_us);
/* 이번 진단 확장: ID는 부호 없는 10진수, 실패는 3필드 ERR 응답이다. */
void protocol_send_mpu6050_id(uint8_t identity);

void protocol_send_hcsr04_ok(uint32_t distance_cm, uint32_t pulse_us);
void protocol_send_hcsr04_out_of_range(uint32_t pulse_us);
void protocol_send_hcsr04_timeout(void);

#endif
