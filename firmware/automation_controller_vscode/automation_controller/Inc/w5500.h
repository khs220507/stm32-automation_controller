#ifndef W5500_DRIVER_H
#define W5500_DRIVER_H

#include "spi2.h"

/* WIZ550io 전용. SPI2 클록/핀/통신 설정과 1 MHz timebase 초기화 후 호출한다.
 * 메인 문맥 전용이며 다른 SPI 접근/인터럽트 호출과 동시에 사용하지 않는다.
 * RDY 대기와 4바이트 프레임에 공통 시간 한도(1~0x7FFFFFFF us)를 적용한다.
 * OK일 때만 version을 갱신한다. OK는 전송 성공이며 값이 0x04인지 별도 확인한다.
 * 프레임 시작 후 실패하면 MCU 재부팅까지 재호출을 차단한다. 자동 복구 없음. */
spi2_result_t w5500_read_version(uint8_t *version, uint32_t timeout_us);

#endif
