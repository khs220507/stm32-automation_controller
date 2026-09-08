#include "app_state.h"

#include <stdint.h>
#include <stddef.h>

#include "board_io.h"
#include "hcsr04.h"
#include "i2c1.h"
#include "mpu6050.h"
#include "protocol.h"
#include "timebase.h"
#include "uart2.h"

#define HCSR04_MEASUREMENT_PERIOD_US 100000U
/* I2C 전송 1회 한도. 실기 검증 전 임시값 10 ms.
 * SLEEP 해제는 읽기/쓰기/재읽기 3회이며 오류 정리 대기는 별도다. */
#define MPU6050_DIAGNOSTIC_TIMEOUT_US 10000U

typedef enum
{
    STATE_INIT = 0,
    STATE_IDLE,
    STATE_AUTO,
    STATE_DIAG,
    STATE_STOP,
    STATE_FAULT
} system_state_t;

static system_state_t current_state;
static hcsr04_measurement_t latest_hcsr04_measurement;
static uint32_t hcsr04_last_measurement_time_us;
static bool i2c_prepared;
static bool hcsr04_prepared;

static const char *app_state_name(system_state_t state);
static void app_state_run_auto(protocol_command_t command);
static void app_state_clear_measurement(void);
static void app_state_check_mpu6050(void);
static void app_state_wake_mpu6050(void);
static const char *app_state_i2c_error(i2c1_result_t result);
static void app_state_check_hcsr04(void);
static void app_state_prepare_hcsr04(void);

void app_state_init(void)
{
    current_state = STATE_INIT;
    app_state_clear_measurement();
    hcsr04_last_measurement_time_us = 0U;
    i2c_prepared = false;
    mpu6050_accel_invalidate();
    hcsr04_prepared = false;
}

void app_state_run(void)
{
    protocol_command_t command;

    if (current_state == STATE_INIT)
    {
        board_io_init();
        /* 대시보드 공통 연결만 준비한다. 센서는 개별 시험 요청 때 초기화한다.
         * I2C BUSY나 센서 미연결이 다른 시험의 기동을 막지 않게 한다. */
        timebase_init();
        uart2_init();
        protocol_init();
        protocol_send_ready();
        current_state = STATE_IDLE;
        return;
    }

    command = protocol_poll_command();
    if (command == PROTOCOL_COMMAND_CONFIG_ACCEL || command == PROTOCOL_COMMAND_READ_ACCEL)
    {
        const char *name = command == PROTOCOL_COMMAND_CONFIG_ACCEL ? "CONFIG_ACCEL" : "READ_ACCEL";
        if (current_state != STATE_IDLE)
        {
            protocol_send_error(name, "INVALID_STATE");
            return;
        }
        if (!i2c_prepared) i2c_prepared = i2c1_init();
        if (!i2c_prepared) { protocol_send_error(name, "NOT_READY"); return; }

        /* UI의 시작 요청에서 설정하고 이후 요청마다 한 표본만 반환한다. */
        mpu6050_accel_t sample;
        const char *error = command == PROTOCOL_COMMAND_CONFIG_ACCEL
            ? mpu6050_accel_configure() : mpu6050_accel_read(&sample);
        if (error != NULL) protocol_send_error(name, error);
        else if (command == PROTOCOL_COMMAND_CONFIG_ACCEL) protocol_send_accel_configured();
        else protocol_send_accel(sample.x, sample.y, sample.z);
        return;
    }
    if (command == PROTOCOL_COMMAND_PING)
    {
        protocol_send_pong();
        return;
    }
    if (command == PROTOCOL_COMMAND_CHECK_HCSR04)
    {
        if (current_state == STATE_AUTO)
            protocol_send_error("CHECK_HCSR04", "INVALID_STATE");
        else
            app_state_check_hcsr04();
        return;
    }
    if (command == PROTOCOL_COMMAND_CHECK_MPU6050)
    {
        /* 이전 START 자동운전과 개별 시험을 섞지 않는다. 진단이 상태를 바꾸지 않는다. */
        if (current_state == STATE_AUTO)
            protocol_send_error("CHECK_MPU6050", "INVALID_STATE");
        else
            app_state_check_mpu6050();
        return;
    }
    if (command == PROTOCOL_COMMAND_WAKE_MPU6050)
    {
        /* 센서 설정 변경은 IDLE에서 명시적인 요청을 받았을 때만 수행한다. */
        if (current_state != STATE_IDLE)
            protocol_send_error("WAKE_MPU6050", "INVALID_STATE");
        else
            app_state_wake_mpu6050();
        return;
    }
    if (command == PROTOCOL_COMMAND_GET_STATUS)
    {
        protocol_send_status(app_state_name(current_state));
        return;
    }

    switch (current_state)
    {
    case STATE_IDLE:
        if (command == PROTOCOL_COMMAND_START)
        {
            app_state_prepare_hcsr04();
            hcsr04_last_measurement_time_us =
                timebase_now_us() - HCSR04_MEASUREMENT_PERIOD_US;
            current_state = STATE_AUTO;
            protocol_send_state("AUTO");
        }
        break;

    case STATE_AUTO:
        app_state_run_auto(command);
        break;

    case STATE_DIAG:
        break;

    case STATE_STOP:
        board_io_set_safe_outputs();
        app_state_clear_measurement();
        current_state = STATE_IDLE;
        protocol_send_state("IDLE");
        break;

    case STATE_FAULT:
        board_io_set_safe_outputs();
        break;

    case STATE_INIT:
    default:
        board_io_set_safe_outputs();
        current_state = STATE_FAULT;
        break;
    }
}

static void app_state_check_mpu6050(void)
{
    uint8_t identity;
    if (!i2c_prepared)
    {
        i2c_prepared = i2c1_init();
        if (!i2c_prepared)
        {
            protocol_send_error("CHECK_MPU6050", "NOT_READY");
            return;
        }
    }
    /* InvenSense RM-MPU-6000A: AD0=LOW 가정의 7비트 주소 0x68,
     * WHO_AM_I 레지스터 0x75. 기대 ID는 0x68이며 실제 읽은 값을 그대로 보낸다.
     * 식별 레지스터 읽기에는 센서 측정 모드 설정을 변경하지 않는다. */
    i2c1_result_t result = i2c1_read_register(0x68U, 0x75U, &identity,
                                           MPU6050_DIAGNOSTIC_TIMEOUT_US);
    if (result == I2C1_RESULT_OK)
    {
        protocol_send_mpu6050_id(identity);
        return;
    }
    protocol_send_error("CHECK_MPU6050", app_state_i2c_error(result));
}

static const char *app_state_i2c_error(i2c1_result_t result)
{
    switch (result)
    {
    case I2C1_RESULT_BUS_BUSY: return "BUS_BUSY";
    case I2C1_RESULT_TIMEOUT: return "TIMEOUT";
    case I2C1_RESULT_NACK: return "NACK";
    case I2C1_RESULT_BUS_ERROR: return "BUS_ERROR";
    case I2C1_RESULT_ARBITRATION_LOST: return "ARBITRATION_LOST";
    case I2C1_RESULT_OVERRUN: return "OVERRUN";
    case I2C1_RESULT_NOT_READY: return "NOT_READY";
    default: return "INTERNAL_ERROR";
    }
}

static void app_state_wake_mpu6050(void)
{
    mpu6050_accel_invalidate();
    uint8_t before, desired, after;
    i2c1_result_t result;

    /* 1. 통신을 준비하고 현재 전원 설정을 읽는다. */
    if (!i2c_prepared)
    {
        i2c_prepared = i2c1_init();
        if (!i2c_prepared)
        {
            protocol_send_error("WAKE_MPU6050", "NOT_READY");
            return;
        }
    }
    /* 로컬 RM-MPU-6000A Rev 3.2 p.44~45, 4.36절: PWR_MGMT_1=0x6B.
     * 주소 0x68 모듈의 레지스터 호환성을 전제한다. 식별값 일치 판정은 아니다. */
    result = i2c1_read_register(0x68U, 0x6BU, &before, MPU6050_DIAGNOSTIC_TIMEOUT_US);
    if (result != I2C1_RESULT_OK) goto failed;
    /* bit 7(DEVICE_RESET)=1이면 리셋 진행 중이므로 다시 쓰지 않는다. */
    if ((before & (0x1U << 7)) != 0U)
    {
        protocol_send_error("WAKE_MPU6050", "SENSOR_RESET");
        return;
    }

    /* 2. bit 6(SLEEP)만 0으로 바꾸고 다른 비트는 보존한다. */
    desired = (uint8_t)(before & ~(0x1U << 6));
    result = i2c1_write_register(0x68U, 0x6BU, desired, MPU6050_DIAGNOSTIC_TIMEOUT_US);
    if (result != I2C1_RESULT_OK) goto failed;

    /* 3. 다시 읽어 SLEEP 해제와 나머지 비트 보존을 확인한다.
     * 이 확인은 측정 안정화/가속도·각속도 수집 성공을 뜻하지 않는다. */
    result = i2c1_read_register(0x68U, 0x6BU, &after, MPU6050_DIAGNOSTIC_TIMEOUT_US);
    if (result != I2C1_RESULT_OK) goto failed;
    if (after != desired)
    {
        protocol_send_error("WAKE_MPU6050", "VERIFY_FAILED");
        return;
    }
    protocol_send_mpu6050_wake(before, after);
    return;

failed:
    /* 쓰기 이후 실패라면 센서 설정이 이미 변경됐을 수 있다. 자동 재시도하지 않는다. */
    protocol_send_error("WAKE_MPU6050", app_state_i2c_error(result));
}

static void app_state_prepare_hcsr04(void)
{
    if (!hcsr04_prepared)
    {
        hcsr04_init();
        hcsr04_prepared = true;
    }
}

static void app_state_check_hcsr04(void)
{
    hcsr04_measurement_t measurement;
    const char *status;
    app_state_prepare_hcsr04();
    hcsr04_measure(&measurement);
    switch (measurement.status)
    {
    case HCSR04_STATUS_OK: status = "OK"; break;
    case HCSR04_STATUS_OUT_OF_RANGE: status = "OUT_OF_RANGE"; break;
    default: status = "TIMEOUT"; break;
    }
    /* OK는 명령 처리 완료, 다음 필드가 센서 측정 결과다. */
    protocol_send_hcsr04_check(status, measurement.distance_cm, measurement.pulse_us);
}

static void app_state_run_auto(protocol_command_t command)
{
    if (command == PROTOCOL_COMMAND_STOP)
    {
        current_state = STATE_STOP;
        protocol_send_state("STOP");
        return;
    }

    if (timebase_elapsed_us(hcsr04_last_measurement_time_us) <
        HCSR04_MEASUREMENT_PERIOD_US)
    {
        return;
    }

    hcsr04_last_measurement_time_us = timebase_now_us();
    hcsr04_measure(&latest_hcsr04_measurement);

    switch (latest_hcsr04_measurement.status)
    {
    case HCSR04_STATUS_OK:
        protocol_send_hcsr04_ok(latest_hcsr04_measurement.distance_cm,
                                latest_hcsr04_measurement.pulse_us);
        break;

    case HCSR04_STATUS_OUT_OF_RANGE:
        protocol_send_hcsr04_out_of_range(latest_hcsr04_measurement.pulse_us);
        break;

    case HCSR04_STATUS_TIMEOUT:
    default:
        protocol_send_hcsr04_timeout();
        break;
    }
}

static const char *app_state_name(system_state_t state)
{
    switch (state)
    {
    case STATE_INIT:
        return "INIT";
    case STATE_IDLE:
        return "IDLE";
    case STATE_AUTO:
        return "AUTO";
    case STATE_DIAG:
        return "DIAG";
    case STATE_STOP:
        return "STOP";
    case STATE_FAULT:
    default:
        return "FAULT";
    }
}

static void app_state_clear_measurement(void)
{
    latest_hcsr04_measurement.status = HCSR04_STATUS_TIMEOUT;
    latest_hcsr04_measurement.distance_cm = 0U;
    latest_hcsr04_measurement.pulse_us = 0U;
}
