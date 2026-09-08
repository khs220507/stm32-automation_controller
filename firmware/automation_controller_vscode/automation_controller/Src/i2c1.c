#include "i2c1.h"

#include "stm32f401xe.h"
#include "timebase.h"

#include <stddef.h>

static bool i2c1_initialized;

static i2c1_result_t i2c1_check_error(void)
{
    uint32_t status = I2C1->SR1;
    /* RM0368 SR1: bit 9=ARLO(중재 상실), bit 8=BERR(버스 오류),
     * bit 11=OVR(수신 초과/송신 부족), bit 10=AF(ACK 응답 없음). */
    if ((status & (0x1U << 9)) != 0U) return I2C1_RESULT_ARBITRATION_LOST;
    if ((status & (0x1U << 8)) != 0U) return I2C1_RESULT_BUS_ERROR;
    if ((status & (0x1U << 11)) != 0U) return I2C1_RESULT_OVERRUN;
    if ((status & (0x1U << 10)) != 0U) return I2C1_RESULT_NACK;
    return I2C1_RESULT_OK;
}

static i2c1_result_t i2c1_wait_sr1(uint32_t flag, uint32_t start, uint32_t timeout_us)
{
    for (;;)
    {
        i2c1_result_t result = i2c1_check_error();
        if (result != I2C1_RESULT_OK) return result;
        if (timebase_elapsed_us(start) >= timeout_us) return I2C1_RESULT_TIMEOUT;
        if ((I2C1->SR1 & flag) != 0U) return I2C1_RESULT_OK;
    }
}

static void i2c1_clear_address(void)
{
    /* RM0368: SR1 다음 SR2를 읽으면 SR1 bit 1(ADDR)이 해제된다.
     * 수신 주소 뒤에는 ACK 설정을 완료한 다음 이 순서를 실행해야 한다. */
    (void)I2C1->SR1;
    (void)I2C1->SR2;
}

static void i2c1_abort(void)
{
    /* 1. 현재 상태와 리셋 후 복원할 설정을 저장한다. */
    uint32_t start = timebase_now_us();
    uint32_t status = I2C1->SR1;
    uint32_t bus_status = I2C1->SR2;
    uint32_t frequency = I2C1->CR2;
    uint32_t clock_control = I2C1->CCR;
    uint32_t rise_time = I2C1->TRISE;

    /* 2. 버스 제어권이 있을 때 STOP을 요청한다.
     * SR1 bit 9(ARLO)=0, SR2 bit 0(MSL)=1 확인. CR1 bit 9=STOP. */
    if (((status & (0x1U << 9)) == 0U) && ((bus_status & (0x1U << 0)) != 0U))
    {
        I2C1->CR1 |= (0x1U << 9);
        /* 3. STOP(bit 9)과 BUSY(bit 1) 해제를 기다린다. 정리 시작부터 최대 1 ms. */
        while (((I2C1->CR1 & (0x1U << 9)) != 0U) ||
               ((I2C1->SR2 & (0x1U << 1)) != 0U))
        {
            if (timebase_elapsed_us(start) >= 1000U) break;
        }
    }

    /* 4. APB1RSTR bit 21(I2C1RST)로 I2C1 내부 상태를 리셋한다.
     * 외부 장치가 SDA/SCL을 LOW로 잡는 고장은 복구하지 못한다. */
    RCC->APB1RSTR |= (0x1U << 21);
    RCC->APB1RSTR &= ~(0x1U << 21);
    /* 5. 저장한 설정을 복원하고 I2C1을 다시 켠다. */
    I2C1->CR2 = frequency;
    I2C1->CCR = clock_control;
    I2C1->TRISE = rise_time;
    I2C1->OAR1 = (0x1U << 14); /* OAR1 bit 14는 1로 유지한다. */
    I2C1->CR1 = (0x1U << 0); /* CR1 bit 0(PE): 주변장치 활성화. */
}

i2c1_result_t i2c1_read_register(uint8_t address, uint8_t reg,
                               uint8_t *value, uint32_t timeout_us)
{
    uint32_t start;
    uint32_t interrupt_mask;
    uint8_t received;
    i2c1_result_t result;

    if ((address > 0x7FU) || (value == NULL) ||
        (timeout_us == 0U) || (timeout_us > 0x7FFFFFFFU))
        return I2C1_RESULT_INVALID_ARGUMENT;

    /* APB1ENR bit 3(TIM5EN), TIM5 CR1 bit 0(CEN), I2C CR1 bit 0(PE).
     * 초기화 누락으로 정지한 타이머를 사용해 무한 대기하는 것을 방지한다. */
    if (!i2c1_initialized || ((RCC->APB1ENR & (0x1U << 3)) == 0U) ||
        ((TIM5->CR1 & (0x1U << 0)) == 0U) || ((I2C1->CR1 & (0x1U << 0)) == 0U))
        return I2C1_RESULT_NOT_READY;

    start = timebase_now_us();
    if (!i2c1_wait_idle(timeout_us)) return I2C1_RESULT_BUS_BUSY;

    /* SR1 bit 8~11(BERR, ARLO, AF, OVR)은 0을 써서 이전 오류를 해제한다.
     * CR1 bit 10(ACK), bit 11(POS)을 해제하여 1바이트 수신을 준비한다. */
    I2C1->SR1 &= ~((0x1U << 8) | (0x1U << 9) | (0x1U << 10) | (0x1U << 11));
    I2C1->CR1 &= ~((0x1U << 10) | (0x1U << 11));
    I2C1->CR1 |= (0x1U << 8); /* CR1 bit 8(START): 시작 조건 요청. */
    result = i2c1_wait_sr1((0x1U << 0), start, timeout_us); /* SR1 bit 0(SB). */
    if (result != I2C1_RESULT_OK) goto failed;

    /* 7비트 주소를 bit 7:1에 배치, bit 0=0은 쓰기 방향이다. */
    I2C1->DR = (uint32_t)address << 1;
    result = i2c1_wait_sr1((0x1U << 1), start, timeout_us); /* SR1 bit 1(ADDR). */
    if (result != I2C1_RESULT_OK) goto failed;
    i2c1_clear_address();
    result = i2c1_wait_sr1((0x1U << 7), start, timeout_us); /* SR1 bit 7(TxE). */
    if (result != I2C1_RESULT_OK) goto failed;
    I2C1->DR = reg;
    result = i2c1_wait_sr1((0x1U << 2), start, timeout_us); /* SR1 bit 2(BTF): 전송 완료. */
    if (result != I2C1_RESULT_OK) goto failed;

    I2C1->CR1 |= (0x1U << 8); /* CR1 bit 8(START): STOP 없이 반복 시작. */
    result = i2c1_wait_sr1((0x1U << 0), start, timeout_us); /* SR1 bit 0(SB). */
    if (result != I2C1_RESULT_OK) goto failed;
    I2C1->DR = ((uint32_t)address << 1) | 0x1U; /* 주소 bit 0=1은 읽기 방향. */
    result = i2c1_wait_sr1((0x1U << 1), start, timeout_us); /* SR1 bit 1(ADDR). */
    if (result != I2C1_RESULT_OK) goto failed;

    /* RM0368 1바이트 수신: ACK=0 → ADDR 해제 → STOP 요청 순서.
     * 이 짧은 구간만 인터럽트를 막고 기존 PRIMASK를 복원한다.
     * 기다리는 루프에서는 인터럽트를 막지 않는다. */
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    I2C1->CR1 &= ~(0x1U << 10); /* CR1 bit 10(ACK): 마지막 바이트에 NACK. */
    i2c1_clear_address();
    I2C1->CR1 |= (0x1U << 9); /* CR1 bit 9(STOP): 마지막 바이트 후 종료. */
    __set_PRIMASK(interrupt_mask);

    result = i2c1_wait_sr1((0x1U << 6), start, timeout_us); /* SR1 bit 6(RxNE). */
    if (result != I2C1_RESULT_OK) goto failed;
    received = (uint8_t)I2C1->DR;
    /* CR1 bit 9(STOP), SR2 bit 1(BUSY) 해제까지 같은 전송 제한시간을 사용한다. */
    while (((I2C1->CR1 & (0x1U << 9)) != 0U) ||
           ((I2C1->SR2 & (0x1U << 1)) != 0U))
    {
        result = i2c1_check_error();
        if (result != I2C1_RESULT_OK) goto failed;
        if (timebase_elapsed_us(start) >= timeout_us)
        {
            result = I2C1_RESULT_TIMEOUT;
            goto failed;
        }
    }
    result = i2c1_check_error();
    if (result != I2C1_RESULT_OK) goto failed;
    *value = received;
    return I2C1_RESULT_OK;

failed:
    i2c1_abort();
    return result;
}

i2c1_result_t i2c1_read_registers(uint8_t address, uint8_t reg,
                               uint8_t *value, uint8_t count, uint32_t timeout_us)
{
    uint32_t start;
    uint32_t interrupt_mask;
    uint8_t received[14];
    uint8_t index = 0U;
    i2c1_result_t result;

    if ((address > 0x7FU) || (value == NULL) || (count < 3U) || (count > 14U) ||
        (timeout_us == 0U) || (timeout_us > 0x7FFFFFFFU))
        return I2C1_RESULT_INVALID_ARGUMENT;

    /* APB1ENR bit 3(TIM5EN), TIM5 CR1 bit 0(CEN), I2C CR1 bit 0(PE).
     * 초기화 누락으로 정지한 타이머를 사용해 무한 대기하는 것을 방지한다. */
    if (!i2c1_initialized || ((RCC->APB1ENR & (0x1U << 3)) == 0U) ||
        ((TIM5->CR1 & (0x1U << 0)) == 0U) || ((I2C1->CR1 & (0x1U << 0)) == 0U))
        return I2C1_RESULT_NOT_READY;

    start = timebase_now_us();
    if (!i2c1_wait_idle(timeout_us)) return I2C1_RESULT_BUS_BUSY;

    /* 1. SR1 bit 8~11 오류 해제. CR1 bit 11(POS)=0, bit 10(ACK)=1로 연속 수신 준비. */
    I2C1->SR1 &= ~((0x1U << 8) | (0x1U << 9) | (0x1U << 10) | (0x1U << 11));
    I2C1->CR1 &= ~(0x1U << 11);
    I2C1->CR1 |= (0x1U << 10);
    I2C1->CR1 |= (0x1U << 8); /* CR1 bit 8(START): 시작 조건 요청. */
    result = i2c1_wait_sr1((0x1U << 0), start, timeout_us); /* SR1 bit 0(SB). */
    if (result != I2C1_RESULT_OK) goto failed;

    /* 7비트 주소를 bit 7:1에 배치, bit 0=0은 쓰기 방향이다. */
    I2C1->DR = (uint32_t)address << 1;
    result = i2c1_wait_sr1((0x1U << 1), start, timeout_us); /* SR1 bit 1(ADDR). */
    if (result != I2C1_RESULT_OK) goto failed;
    i2c1_clear_address();
    result = i2c1_wait_sr1((0x1U << 7), start, timeout_us); /* SR1 bit 7(TxE). */
    if (result != I2C1_RESULT_OK) goto failed;
    I2C1->DR = reg;
    result = i2c1_wait_sr1((0x1U << 2), start, timeout_us); /* SR1 bit 2(BTF): 전송 완료. */
    if (result != I2C1_RESULT_OK) goto failed;

    I2C1->CR1 |= (0x1U << 8); /* CR1 bit 8(START): STOP 없이 반복 시작. */
    result = i2c1_wait_sr1((0x1U << 0), start, timeout_us); /* SR1 bit 0(SB). */
    if (result != I2C1_RESULT_OK) goto failed;
    I2C1->DR = ((uint32_t)address << 1) | 0x1U; /* 주소 bit 0=1은 읽기 방향. */
    result = i2c1_wait_sr1((0x1U << 1), start, timeout_us); /* SR1 bit 1(ADDR). */
    if (result != I2C1_RESULT_OK) goto failed;

    /* 2. ADDR 해제 후 앞부분을 읽는다. 마지막 3바이트는 아래에서 따로 종료한다.
     * RM0368 Rev 6 p.483~484, N > 2-byte reception. */
    i2c1_clear_address();
    while (index < count - 3U)
    {
        result = i2c1_wait_sr1((0x1U << 6), start, timeout_us); /* SR1 bit 6(RxNE). */
        if (result != I2C1_RESULT_OK) goto failed;
        received[index++] = (uint8_t)I2C1->DR;
    }

    /* 3. BTF=1에서 SCL이 멈춘 동안 ACK를 끄고 N-2를 읽는다.
     * SR1 bit 2(BTF), CR1 bit 10(ACK). 대기 루프에서는 인터럽트를 막지 않는다. */
    result = i2c1_wait_sr1((0x1U << 2), start, timeout_us);
    if (result != I2C1_RESULT_OK) goto failed;
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    I2C1->CR1 &= ~(0x1U << 10);
    received[index++] = (uint8_t)I2C1->DR;
    __set_PRIMASK(interrupt_mask);

    /* 4. 마지막 바이트까지 도착하면 STOP 요청 후 N-1, N을 읽는다. CR1 bit 9(STOP). */
    result = i2c1_wait_sr1((0x1U << 2), start, timeout_us);
    if (result != I2C1_RESULT_OK) goto failed;
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    I2C1->CR1 |= (0x1U << 9);
    received[index++] = (uint8_t)I2C1->DR;
    received[index++] = (uint8_t)I2C1->DR;
    __set_PRIMASK(interrupt_mask);

    /* CR1 bit 9(STOP), SR2 bit 1(BUSY) 해제까지 같은 전송 제한시간을 사용한다. */
    while (((I2C1->CR1 & (0x1U << 9)) != 0U) ||
           ((I2C1->SR2 & (0x1U << 1)) != 0U))
    {
        result = i2c1_check_error();
        if (result != I2C1_RESULT_OK) goto failed;
        if (timebase_elapsed_us(start) >= timeout_us)
        {
            result = I2C1_RESULT_TIMEOUT;
            goto failed;
        }
    }
    result = i2c1_check_error();
    if (result != I2C1_RESULT_OK) goto failed;
    /* 5. STOP까지 성공한 경우에만 호출자의 버퍼를 갱신한다. */
    for (index = 0U; index < count; index++) value[index] = received[index];
    return I2C1_RESULT_OK;

failed:
    i2c1_abort();
    return result;
}

i2c1_result_t i2c1_write_register(uint8_t address, uint8_t reg,
                                uint8_t value, uint32_t timeout_us)
{
    uint32_t start;
    i2c1_result_t result;

    /* 1. 인수와 초기화 상태를 확인한다. 읽기 함수와 같은 전제다. */
    if ((address > 0x7FU) || (timeout_us == 0U) || (timeout_us > 0x7FFFFFFFU))
        return I2C1_RESULT_INVALID_ARGUMENT;
    /* APB1ENR bit 3=TIM5EN, TIM5 CR1 bit 0=CEN, I2C CR1 bit 0=PE. */
    if (!i2c1_initialized || ((RCC->APB1ENR & (0x1U << 3)) == 0U) ||
        ((TIM5->CR1 & (0x1U << 0)) == 0U) || ((I2C1->CR1 & (0x1U << 0)) == 0U))
        return I2C1_RESULT_NOT_READY;

    /* 2. 버스가 비면 START를 요청한다. 전체 과정에 같은 제한시간을 쓴다. */
    start = timebase_now_us();
    if (!i2c1_wait_idle(timeout_us)) return I2C1_RESULT_BUS_BUSY;
    /* SR1 bit 8~11: BERR/ARLO/AF/OVR 이전 오류 해제. */
    I2C1->SR1 &= ~((0x1U << 8) | (0x1U << 9) | (0x1U << 10) | (0x1U << 11));
    I2C1->CR1 |= (0x1U << 8); /* CR1 bit 8(START): 시작 요청. */
    result = i2c1_wait_sr1((0x1U << 0), start, timeout_us); /* SR1 bit 0(SB). */
    if (result != I2C1_RESULT_OK) goto failed;

    /* 3. 장치 주소 + 쓰기 방향(bit 0=0)을 보내고 ACK를 확인한다. */
    I2C1->DR = (uint32_t)address << 1;
    result = i2c1_wait_sr1((0x1U << 1), start, timeout_us); /* SR1 bit 1(ADDR). */
    if (result != I2C1_RESULT_OK) goto failed;
    i2c1_clear_address();

    /* 4. 레지스터 주소를 보낸 뒤, 쓸 데이터 한 바이트를 보낸다.
     * RM0368 Rev 6 p.481~482 Controller transmitter: DR 기록 후 BTF 확인. */
    result = i2c1_wait_sr1((0x1U << 7), start, timeout_us); /* SR1 bit 7(TxE). */
    if (result != I2C1_RESULT_OK) goto failed;
    I2C1->DR = reg;
    result = i2c1_wait_sr1((0x1U << 2), start, timeout_us); /* SR1 bit 2(BTF). */
    if (result != I2C1_RESULT_OK) goto failed;
    I2C1->DR = value;
    result = i2c1_wait_sr1((0x1U << 2), start, timeout_us); /* SR1 bit 2(BTF). */
    if (result != I2C1_RESULT_OK) goto failed;

    /* 5. STOP을 요청하고 버스 종료까지 확인한다. */
    I2C1->CR1 |= (0x1U << 9); /* CR1 bit 9(STOP). */
    while (((I2C1->CR1 & (0x1U << 9)) != 0U) ||
           ((I2C1->SR2 & (0x1U << 1)) != 0U)) /* SR2 bit 1(BUSY). */
    {
        result = i2c1_check_error();
        if (result != I2C1_RESULT_OK) goto failed;
        if (timebase_elapsed_us(start) >= timeout_us)
        {
            result = I2C1_RESULT_TIMEOUT;
            goto failed;
        }
    }
    result = i2c1_check_error();
    if (result != I2C1_RESULT_OK) goto failed;
    return I2C1_RESULT_OK;

failed:
    /* 실패 원인을 유지하며 내부 상태를 정리한다. 자동 재전송은 하지 않는다. */
    i2c1_abort();
    return result;
}

bool i2c1_wait_idle(uint32_t timeout_us)
{
    uint32_t start_time_us;

    if (timeout_us > 0x7FFFFFFFU)
    {
        return false;
    }

    start_time_us = timebase_now_us();
    /* RM0368 I2C_SR2 비트 1(BUSY): 0=버스 유휴, 1=통신 진행 중.
     * 읽어서 확인하며 소프트웨어로 이 비트를 지우지 않는다. */
    while ((I2C1->SR2 & (0x1U << 1)) != 0U)
    {
        /* unsigned 차이로 TIM5 카운터가 한 번 넘치는 경우도 처리한다.
         * 제한시간이 0이고 BUSY이면 첫 검사에서 바로 실패한다. */
        if (timebase_elapsed_us(start_time_us) >= timeout_us)
        {
            return false;
        }
    }

    return true;
}

bool i2c1_init(void)
{
    uint32_t apb1_prescaler;
    uint32_t pclk1_hz;

    i2c1_initialized = false;
    i2c1_pins_init();

    /* 부팅 시 한 번만 초기화한다. 전송 중 재초기화 용도가 아니다.
     * APB1RSTR의 비트 21(I2C1RST)을 1로 설정한 뒤 0으로 해제한다. */
    RCC->APB1RSTR |= (0x1U << 21);
    RCC->APB1RSTR &= ~(0x1U << 21);

    SystemCoreClockUpdate();
    /* CFGR의 비트 12:10(PPRE1)을 추출하여 APB1 분주 설정을 읽는다. */
    apb1_prescaler = (RCC->CFGR & (0x7U << 10)) >> 10;
    pclk1_hz = SystemCoreClock >> APBPrescTable[apb1_prescaler];

    /* RM0368: Standard mode 입력 클록은 2 MHz 이상이다.
     * F401 APB1 상한 42 MHz 및 정수 MHz 입력만 지원한다.
     * 실패하면 리셋 상태(PE=0)를 유지한다. */
    if ((pclk1_hz < 2000000U) || (pclk1_hz > 42000000U) ||
        ((pclk1_hz % 1000000U) != 0U))
    {
        return false;
    }

    /* RM0368 I2C_CR2/CCR/TRISE, PE=0에서 설정한다.
     * 현재 HSI 명목 16 MHz, APB1 분주 1: FREQ=16, CCR=80, TRISE=17.
     * CCR = ceil(PCLK1 / (2 * 100000 Hz)), FS=0(Standard mode).
     * TRISE = PCLK1[MHz] * 1 us + 1 (Standard mode 상승시간 한계).
     * 실제 SCL 주파수에는 발진기 오차와 버스 상승시간이 영향을 준다. */
    I2C1->CR2 = pclk1_hz / 1000000U;
    I2C1->CCR = (pclk1_hz + 199999U) / 200000U;
    I2C1->TRISE = (pclk1_hz / 1000000U) + 1U;
    /* OAR1 bit 14는 소프트웨어가 1로 유지해야 한다. */
    I2C1->OAR1 = (0x1U << 14);
    /* CR1의 비트 0(PE)을 1로 설정하여 I2C1을 활성화한다. */
    I2C1->CR1 = (0x1U << 0);
    i2c1_initialized = true;
    return true;
}

void i2c1_pins_init(void)
{
    /* AHB1ENR의 비트 1(GPIOBEN)을 1로 설정하여 GPIOB 클록을 활성화한다.
     * GPIOB 레지스터에 접근하기 전에 AHB1ENR을 한 번 읽고 값을 버린다. */
    RCC->AHB1ENR |= (0x1U << 1);
    (void)RCC->AHB1ENR;

    /* APB1ENR의 비트 21(I2C1EN)을 설정하여 I2C1 주변장치 클록을 공급한다.
     * 클록 활성화 후 레지스터를 읽어 지연을 확보한다.
     * 통신 속도 설정과 CR1의 PE 활성화는 별도 단계다. */
    RCC->APB1ENR |= (0x1U << 21);
    (void)RCC->APB1ENR;

    /* OTYPER는 핀당 1비트다. 비트 8과 9를 1로 설정한다.
     * PB8과 PB9를 오픈 드레인 출력으로 설정하며 다른 비트는 유지한다. */
    GPIOB->OTYPER |= (0x1U << 8) | (0x1U << 9);

    /* OSPEEDR는 핀당 2비트다. PB8은 17:16, PB9는 19:18에 해당한다.
     * 0x3(2진수 11)으로 각 필드를 지운 뒤 0x2(2진수 10)를 넣는다.
     * 두 핀의 출력 속도를 고속으로 설정한다. I2C 통신 주파수 설정은 아니다. */
    GPIOB->OSPEEDR &= ~((0x3U << 16) | (0x3U << 18));
    GPIOB->OSPEEDR |=  (0x2U << 16) | (0x2U << 18);

    /* PUPDR는 핀당 2비트다. PB8의 17:16, PB9의 19:18을 00으로 지운다.
     * 내부 풀업과 풀다운을 사용하지 않는다. 다른 핀의 설정은 유지한다.
     * 모듈에 적절한 외부 SDA/SCL 풀업 저항이 연결되어 있다고 가정한다. */
    GPIOB->PUPDR &= ~((0x3U << 16) | (0x3U << 18));

    /* AFR[1]은 핀 8~15를 담당하는 AFRH이며, 핀당 4비트를 사용한다.
     * PB8은 비트 3:0, PB9는 비트 7:4에 해당한다.
     * 0xF(2진수 1111)로 각 필드를 지운 뒤 0x4(2진수 0100)를 넣는다.
     * AF4를 선택하여 PB8은 I2C1_SCL, PB9는 I2C1_SDA에 연결한다.
     * 다른 핀의 설정은 유지하며, 하위 8비트의 설정값은 0x44다. */
    GPIOB->AFR[1] &= ~((0xFU << 0) | (0xFU << 4));
    GPIOB->AFR[1] |=  (0x4U << 0) | (0x4U << 4);

    /* MODER는 핀당 2비트다. PB8은 비트 17:16, PB9는 비트 19:18이다.
     * 0x3(2진수 11)으로 각 필드를 지운 뒤 0x2(2진수 10)를 넣는다.
     * 두 핀을 대체 기능(Alternate Function) 모드로 설정한다.
     * AFR에서 선택한 AF4를 사용하도록 하며, 다른 핀의 설정은 유지한다. */
    GPIOB->MODER &= ~((0x3U << 16) | (0x3U << 18));
    GPIOB->MODER |=  (0x2U << 16) | (0x2U << 18);
}
