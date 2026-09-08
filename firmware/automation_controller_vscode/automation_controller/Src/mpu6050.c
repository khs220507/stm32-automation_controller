#include "mpu6050.h"

#include <stdbool.h>
#include <stddef.h>
#include "i2c1.h"
#include "timebase.h"

/* 현재 모듈: 장치 주소 0x68, 사용자가 확인한 WHO_AM_I=0x72.
 * MPU6050 레지스터 호환을 전제로 하며 실제 축 방향/감도는 실기 검증 대상이다. */
static bool configured;
static uint32_t configured_at;
static const uint8_t registers[] = {0x6BU, 0x6CU, 0x1CU, 0x1AU, 0x19U, 0x38U};
static uint8_t expected[sizeof registers];

static const char *bus_error(i2c1_result_t result)
{
    switch (result)
    {
    case I2C1_RESULT_OK: return NULL;
    case I2C1_RESULT_BUS_BUSY: return "BUS_BUSY";
    case I2C1_RESULT_TIMEOUT: return "TIMEOUT";
    case I2C1_RESULT_NACK: return "NACK";
    case I2C1_RESULT_BUS_ERROR: return "BUS_ERROR";
    case I2C1_RESULT_ARBITRATION_LOST: return "ARBITRATION_LOST";
    case I2C1_RESULT_OVERRUN: return "OVERRUN";
    case I2C1_RESULT_NOT_READY: return "NOT_READY";
    default: return "INVALID_ARGUMENT";
    }
}

static const char *read_byte(uint8_t reg, uint8_t *value)
{
    return bus_error(i2c1_read_register(0x68U, reg, value, 10000U));
}

void mpu6050_accel_invalidate(void)
{
    configured = false;
}

const char *mpu6050_accel_configure(void)
{
    uint8_t before, after;
    const char *error;
    configured = false;

    /* 1. 다른 장치에 설정을 쓰지 않도록 현재 모듈의 식별값을 먼저 확인한다. */
    error = read_byte(0x75U, &before);
    if (error != NULL) return error;
    if (before != 0x72U) return "ID_MISMATCH";

    /* 2. 설정을 읽고 필요한 비트만 변경한 뒤 다시 읽어 확인한다.
     * 로컬 RM-MPU-6000A Rev 3.2 p.10~14, p.29, p.44~46. */
    for (uint8_t i = 0U; i < sizeof registers; i++)
    {
        error = read_byte(registers[i], &before);
        if (error != NULL) return error;
        switch (registers[i])
        {
        case 0x6BU:
            /* PWR_MGMT_1 bit 7=리셋 진행, bit 6=SLEEP, bit 5=CYCLE.
             * bit 2:0(CLKSEL)=0: 내부 발진기. 연속 측정 모드로 깨어난다. */
            if ((before & (0x1U << 7)) != 0U) return "SENSOR_RESET";
            expected[i] = (uint8_t)(before & ~((0x1U << 6) | (0x1U << 5) | (0x7U << 0)));
            break;
        case 0x6CU:
            /* PWR_MGMT_2 bit 5:3(STBY_XA/YA/ZA)=0: 가속도 세 축 사용. */
            expected[i] = (uint8_t)(before & ~(0x7U << 3));
            break;
        case 0x1CU:
            /* ACCEL_CONFIG bit 7:5=자가시험 끔, bit 4:3(AFS_SEL)=0: ±2g. */
            expected[i] = (uint8_t)(before & ~((0x7U << 5) | (0x3U << 3)));
            break;
        case 0x1AU:
            /* CONFIG bit 5:3(EXT_SYNC_SET)=0, bit 2:0(DLPF_CFG)=3: 가속도 44 Hz.
             * 이 필터 설정은 자이로에도 공유된다. */
            expected[i] = (uint8_t)((before & ~((0x7U << 3) | (0x7U << 0))) | (0x3U << 0));
            break;
        case 0x19U:
            /* SMPLRT_DIV bit 7:0=9: 1 kHz / (1+9) = 센서 내부 갱신 100 Hz. */
            expected[i] = 9U;
            break;
        default:
            /* INT_ENABLE bit 0(DATA_RDY_EN)=1. MCU 외부 인터럽트 대신 상태를 읽는다. */
            expected[i] = (uint8_t)(before | (0x1U << 0));
            break;
        }
        error = bus_error(i2c1_write_register(0x68U, registers[i], expected[i], 10000U));
        if (error != NULL) return error;
        error = read_byte(registers[i], &after);
        if (error != NULL) return error;
        if (after != expected[i]) return "VERIFY_FAILED";
    }
    /* 3. 학습용 초기 대기 100 ms. 기다리는 동안 메인 루프는 계속 실행한다.
     * 정밀 안정화 보증값이 아니며 이후 DATA_RDY도 별도로 확인한다. */
    configured_at = timebase_now_us();
    configured = true;
    return NULL;
}

static int16_t signed_axis(uint8_t high, uint8_t low)
{
    /* 상위/하위 바이트 결합 후 16비트 2의 보수 해석. 범위 밖 signed 캐스팅을 피한다. */
    uint32_t bits = ((uint32_t)high << 8) | low;
    int32_t value = (bits & (0x1U << 15)) != 0U ? (int32_t)bits - 65536 : (int32_t)bits;
    return (int16_t)value;
}

const char *mpu6050_accel_read(mpu6050_accel_t *sample)
{
    uint8_t status, data[6];
    const char *error;
    if (sample == NULL) return "INVALID_ARGUMENT";
    if (!configured) return "NOT_CONFIGURED";

    /* 1. 리셋/설정 변경 후 잘못된 감도로 환산하지 않도록 현재 설정을 확인한다. */
    for (uint8_t i = 0U; i < sizeof registers; i++)
    {
        error = read_byte(registers[i], &status);
        if (error != NULL) goto failed;
        if (status != expected[i]) { error = "CONFIG_CHANGED"; goto failed; }
    }
    /* I2C 전송의 타이머 동작 검사 이후 초기 대기를 판정한다. */
    if (timebase_elapsed_us(configured_at) < 100000U) return "WARMING_UP";

    /* 2. INT_STATUS(0x3A)를 읽어 이전 플래그를 지운 뒤 새 DATA_RDY(bit 0)를 기다린다.
     * 센서 100 Hz 갱신에 대해 최대 30 ms. 로컬 매뉴얼 p.30. */
    error = read_byte(0x3AU, &status);
    if (error != NULL) goto failed;
    uint32_t start = timebase_now_us();
    for (;;)
    {
        uint32_t elapsed = timebase_elapsed_us(start);
        if (elapsed >= 30000U) { error = "DATA_TIMEOUT"; goto failed; }
        uint32_t remaining = 30000U - elapsed;
        error = bus_error(i2c1_read_register(0x68U, 0x3AU, &status,
                                          remaining < 10000U ? remaining : 10000U));
        if (error != NULL) goto failed;
        if ((status & (0x1U << 0)) != 0U) break;
    }

    /* 3. 0x3B~0x40을 한 번에 읽어 같은 시점의 X/Y/Z를 얻는다. 로컬 매뉴얼 p.31. */
    error = bus_error(i2c1_read_registers(0x68U, 0x3BU, data, 6U, 10000U));
    if (error != NULL) goto failed;
    mpu6050_accel_t received = {signed_axis(data[0], data[1]),
                              signed_axis(data[2], data[3]), signed_axis(data[4], data[5])};
    *sample = received;
    return NULL;

failed:
    /* 실패한 표본은 내보내지 않는다. 다음 측정 시작에서 설정부터 다시 확인한다. */
    configured = false;
    return error;
}
