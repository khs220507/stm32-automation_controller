#ifndef SPI2_DRIVER_H
#define SPI2_DRIVER_H

#include <stdint.h>

typedef enum
{
    SPI2_RESULT_OK = 0,
    SPI2_RESULT_INVALID_ARGUMENT,
    SPI2_RESULT_NOT_READY,
    SPI2_RESULT_TIMEOUT,
    SPI2_RESULT_HARDWARE_ERROR,
    SPI2_RESULT_DIRTY_STATE,
} spi2_result_t;

/* 학습 1단계: GPIOB·GPIOC·SPI2 클록을 켠다. */
void spi2_enable_clocks(void);

/* 학습 2단계: 클록 공급 후, SPI2가 비활성화된 부팅 시 한 번 호출한다.
 * GPIO와 AF5만 설정한다. 이후 spi2_configure()로 통신을 설정한다. */
void spi2_pins_init(void);

/* 학습 3단계: 클록·핀 초기화 후 부팅 시 한 번 호출한다. 진행 중인 전송에서는 호출 금지.
 * 현재 프로젝트의 PCLK1=명목 16 MHz 조건에서 Mode 0, 8비트, MSB 우선, 1 MHz.
 * SPI2를 활성화하되 데이터를 전송하지 않으며 CS는 GPIO로 따로 제어한다. */
void spi2_configure(void);

/* spi2_configure(), timebase_init() 이후 메인 루프에서만 사용. ISR/동시 접근 금지.
 * 1 MHz timebase가 계속 동작해야 한다. timeout_us는 전체 대기 한도(1~0x7FFFFFFF us).
 * CS는 호출자가 프레임 단위로 제어한다. OK일 때만 rx를 갱신하며 마지막 클록 종료도 확인한다.
 * 전송 오류/시간 초과/잔여 데이터 발견 후에는 CS를 해제하고 MCU 재부팅 후 재시도한다.
 * OK는 MCU의 송수신 완료이며 상대 연결이나 응답 내용의 유효성을 보증하지 않는다. */
spi2_result_t spi2_transfer_byte(uint8_t tx, uint8_t *rx, uint32_t timeout_us);

#endif
