#ifndef I2C1_DRIVER_H
#define I2C1_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    I2C1_RESULT_OK = 0,
    I2C1_RESULT_BUS_BUSY,
    I2C1_RESULT_TIMEOUT,
    I2C1_RESULT_NACK,
    I2C1_RESULT_BUS_ERROR,
    I2C1_RESULT_ARBITRATION_LOST,
    I2C1_RESULT_OVERRUN,
    I2C1_RESULT_NOT_READY,
    I2C1_RESULT_INVALID_ARGUMENT,
} i2c1_result_t;

/* 아래 초기화는 부팅 시 한 번 호출한다.
 * 100 kHz Standard mode 설정. 지원하지 않는 PCLK1이면 false를 반환한다. */
bool i2c1_init(void);

/* i2c1_init() 성공 및 timebase_init() 이후, 전송 시작 전에 호출한다.
 * timebase가 1 MHz로 계속 동작해야 한다. 동시 I2C 접근은 지원하지 않는다.
 * timeout_us는 0~0x7FFFFFFF us. 0이면 현재 상태만 검사한다.
 * 버스 유휴 시 true, 제한시간 초과 또는 잘못된 인수이면 false다.
 * START/STOP 발생이나 버스 복구는 수행하지 않는다. */
bool i2c1_wait_idle(uint32_t timeout_us);

/* 7비트 주소 장치의 8비트 레지스터를 1바이트 읽는다.
 * 단일 컨트롤러, 메인 루프 전용이며 ISR/동시 호출은 지원하지 않는다.
 * i2c1_init(), timebase_init() 이후 1 MHz timebase가 계속 동작해야 한다.
 * timeout_us(1~0x7FFFFFFF)는 버스 대기부터 STOP까지 전체 전송 한도다.
 * 실패 정리는 추가로 최대 1000 us 대기한다. value는 성공할 때만 갱신한다. */
i2c1_result_t i2c1_read_register(uint8_t address, uint8_t reg,
                               uint8_t *value, uint32_t timeout_us);

/* 위 읽기 함수와 같은 초기화/동시성/시간 제한 조건으로 1바이트를 쓴다.
 * OK는 버스 전송 성공이며 센서 설정 적용 여부는 별도 읽기로 확인한다.
 * 실패해도 센서가 이미 값을 받았을 수 있으므로 자동 재시도하지 않는다. */
i2c1_result_t i2c1_write_register(uint8_t address, uint8_t reg,
                                uint8_t value, uint32_t timeout_us);

/* Configure PB8/PB9 as the I2C1 SCL/SDA alternate-function pins. */
void i2c1_pins_init(void);

#endif
