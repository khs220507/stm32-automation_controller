#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} mpu6050_accel_t;

/* 메인 루프 전용. I2C1/TIM5 초기화 후 호출한다.
 * NULL=성공, 나머지는 UART로 보낼 오류 문자열. 실패 시 측정값은 보존한다.
 * 설정은 최대 19회, 읽기는 최대 8회 I2C 전송 + 새 데이터 대기 30 ms.
 * 전송별 제한 10 ms, 실패 정리는 추가 1 ms. 자동 재시도는 하지 않는다. */
const char *mpu6050_accel_configure(void);
const char *mpu6050_accel_read(mpu6050_accel_t *sample);
void mpu6050_accel_invalidate(void);

#endif
