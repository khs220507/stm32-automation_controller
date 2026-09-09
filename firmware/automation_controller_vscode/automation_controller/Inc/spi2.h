#ifndef SPI2_DRIVER_H
#define SPI2_DRIVER_H

/* 학습 1단계: GPIOB·GPIOC·SPI2 클록을 켠다. */
void spi2_enable_clocks(void);

/* 학습 2단계: 클록 공급 후, SPI2가 비활성화된 부팅 시 한 번 호출한다.
 * GPIO와 AF5만 설정한다. 이후 spi2_configure()로 통신을 설정한다. */
void spi2_pins_init(void);

/* 학습 3단계: 클록·핀 초기화 후 부팅 시 한 번 호출한다. 진행 중인 전송에서는 호출 금지.
 * 현재 프로젝트의 PCLK1=명목 16 MHz 조건에서 Mode 0, 8비트, MSB 우선, 1 MHz.
 * SPI2를 활성화하되 데이터를 전송하지 않으며 CS는 GPIO로 따로 제어한다. */
void spi2_configure(void);

#endif
