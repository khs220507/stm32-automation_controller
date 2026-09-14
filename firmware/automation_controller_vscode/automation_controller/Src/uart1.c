#include "uart1.h"

#include "stm32f401xe.h"
#include "timebase.h"
#include <stdbool.h>

#define UART1_LINE_CAPACITY 64U
#define UART1_RECEIVE_TIMEOUT_US 100000U
#define UART1_TRANSMIT_TIMEOUT_US 20000U

static uint8_t incoming[UART1_LINE_CAPACITY];
static uint8_t incoming_length;
static bool discard_line;
static uint32_t last_byte_us;
static volatile uint8_t received[UART1_LINE_CAPACITY];
static volatile uint8_t received_length;
static uint8_t received_position;
static volatile bool transmitting;
static char outgoing[128];
static uint8_t outgoing_length;
static bool outgoing_overflow;
volatile uint32_t uart1_received_bytes;
volatile uint32_t uart1_receive_errors;
volatile uint32_t uart1_dropped_lines;
volatile uint32_t uart1_transmit_errors;

void uart1_enable_clocks(void)
{
    /* RM0368 Rev 6, 6.3.9절 p.118~119: GPIOBEN 비트 1, GPIOB 클록 활성화. */
    RCC->AHB1ENR |= (0x1U << 1);
    (void)RCC->AHB1ENR;

    /* RM0368 Rev 6, 6.3.12절 p.122~123: USART1EN 비트 4, USART1 클록 활성화. */
    RCC->APB2ENR |= (0x1U << 4);
    (void)RCC->APB2ENR;
}

void uart1_init(void)
{
    uart1_enable_clocks();
    NVIC_DisableIRQ(USART1_IRQn);
    USART1->CR1 = 0U;

    /* RM0368 §8.4.9 p.162: AFRL6[27:24]/7[31:28]=AF7. */
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~((0xFU << 24) | (0xFU << 28)))
        | (0x7U << 24) | (0x7U << 28);
    /* §8.4.1~4 p.158~160: PB7 AF 입력, PB6는 첫 응답 전까지 입력 유지. */
    GPIOB->MODER = (GPIOB->MODER & ~((0x3U << 12) | (0x3U << 14)))
        | (0x2U << 14);
    GPIOB->OTYPER &= ~(0x1U << 6);
    GPIOB->OSPEEDR &= ~(0x3U << 12);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~((0x3U << 12) | (0x3U << 14)))
        | (0x1U << 14);

    /* §19.6.3~6 p.551~556: PCLK2=16 MHz, 115200 8N1, OVER8/M/PCE=0.
     * BRR[15:4]/[3:0]=8/11: 16000000/139=115107.9 baud (-0.080%).
     * STOP[13:12]=00, HDSEL[3]=0: 외부 변환기의 TX/RX 두 선 사용. */
    USART1->BRR = 139U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;
    incoming_length = 0U;
    discard_line = false;
    received_length = 0U;
    received_position = 0U;
    outgoing_length = 0U;
    outgoing_overflow = false;
    transmitting = false;
    last_byte_us = timebase_now_us();
    (void)USART1->SR;
    (void)USART1->DR;
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_SetPriority(USART1_IRQn, 2U);
    /* §19.6.4: UE[13], RXNEIE[5], RE[2]. TE[3]는 첫 응답에서 활성화. */
    USART1->CR1 = (0x1U << 13) | (0x1U << 5) | (0x1U << 2);
    NVIC_EnableIRQ(USART1_IRQn);
}

void USART1_IRQHandler(void)
{
    /* §19.6.1 p.549~550: SR 다음 DR 읽기로 RXNE/ORE/NF/FE/PE 정리. */
    uint32_t status = USART1->SR;
    if ((status & ((0x1U << 5) | 0xFU)) == 0U)
        return;
    uint8_t value = (uint8_t)USART1->DR;
    uint32_t now = timebase_now_us();
    uart1_received_bytes++;
    if (transmitting)
        return;

    if ((uint32_t)(now - last_byte_us) >= UART1_RECEIVE_TIMEOUT_US)
    {
        incoming_length = 0U;
        discard_line = false;
    }
    last_byte_us = now;
    if ((status & 0xFU) != 0U)
    {
        uart1_receive_errors++;
        discard_line = true;
    }
    if (incoming_length >= UART1_LINE_CAPACITY)
        discard_line = true;

    if (discard_line == false)
    {
        incoming[incoming_length] = value;
        incoming_length++;
    }
    if (value != (uint8_t)'\n')
        return;

    if ((discard_line == false) && (received_length == 0U))
    {
        for (uint8_t i = 0U; i < incoming_length; i++)
            received[i] = incoming[i];
        received_length = incoming_length;
    }
    else
    {
        uart1_dropped_lines++;
    }
    incoming_length = 0U;
    discard_line = false;
}

uint8_t uart1_try_read_byte(uint8_t *value)
{
    if (received_length == 0U)
        return 0U;
    *value = received[received_position];
    received_position++;
    if (received_position == received_length)
    {
        received_position = 0U;
        received_length = 0U;
    }
    return 1U;
}

static void uart1_send_line(void)
{
    /* 자동 방향 변환기와 USB 송신기의 버스 반환 시간을 확보한다. */
    transmitting = true;
    incoming_length = 0U;
    discard_line = false;
    timebase_delay_us(1000U);
    /* PB7에서 명령을 받은 뒤에만 PB6를 AF 출력으로 전환한다. */
    USART1->CR1 |= (0x1U << 3);
    GPIOB->MODER = (GPIOB->MODER & ~(0x3U << 12)) | (0x2U << 12);
    uint32_t start = timebase_now_us();
    bool success = true;
    for (uint8_t i = 0U; i < outgoing_length; i++)
    {
        /* SR.TXE[7]: 데이터 레지스터가 비어 있을 때 다음 바이트 기록. */
        while ((USART1->SR & (0x1U << 7)) == 0U)
        {
            if (timebase_elapsed_us(start) >= UART1_TRANSMIT_TIMEOUT_US)
            {
                success = false;
                break;
            }
        }
        if (success == false)
            break;
        USART1->DR = (uint8_t)outgoing[i];
    }
    /* SR.TC[6]: 마지막 정지 비트까지 나간 뒤 수신을 재개한다. */
    while ((USART1->SR & (0x1U << 6)) == 0U)
    {
        if (timebase_elapsed_us(start) >= UART1_TRANSMIT_TIMEOUT_US)
        {
            success = false;
            break;
        }
    }
    if (success == false)
    {
        uart1_transmit_errors++;
        USART1->CR1 &= ~(0x1U << 13);
        USART1->CR1 |= (0x1U << 13);
    }
    timebase_delay_us(1000U);
    transmitting = false;
}

void uart1_write_text(const char *text)
{
    while (*text != '\0')
    {
        char value = *text++;
        if (outgoing_length < sizeof(outgoing))
            outgoing[outgoing_length++] = value;
        else
            outgoing_overflow = true;
        if (value != '\n')
            continue;
        if (outgoing_overflow == false)
            uart1_send_line();
        else
            uart1_transmit_errors++;
        outgoing_length = 0U;
        outgoing_overflow = false;
    }
}
