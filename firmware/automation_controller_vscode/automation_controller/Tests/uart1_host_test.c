#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Src/uart1.c"

host_usart_t host_usart;
host_rcc_t host_rcc;
host_gpio_t host_gpio;
static uint32_t now;

uint32_t timebase_now_us(void)
{
    return now++;
}

uint32_t timebase_elapsed_us(uint32_t start)
{
    return now++ - start;
}

void timebase_delay_us(uint32_t duration)
{
    now += duration;
}

static void receive(const char *text)
{
    while (*text != '\0')
    {
        host_usart.SR = (0x1U << 5);
        host_usart.DR = (uint8_t)*text++;
        USART1_IRQHandler();
    }
}

static void expect(const char *expected)
{
    uint8_t value;
    char actual[65];
    unsigned count = 0U;
    while (true)
    {
        uint8_t available = uart1_try_read_byte(&value);
        if (available == 0U)
            break;
        assert(count < sizeof(actual) - 1U);
        actual[count++] = (char)value;
    }
    actual[count] = '\0';
    assert(strcmp(actual, expected) == 0);
}

int main(void)
{
    host_gpio.MODER = 0xA9000280U;
    uart1_init();
    assert((host_gpio.MODER & (0x3U << 12)) == 0U);
    receive("PI");
    expect("");
    receive("NG\r\n");
    expect("PING\r\n");
    receive("PING\r\nCHECK_W5500\r\n");
    expect("PING\r\n");
    assert(uart1_dropped_lines == 1U);
    receive("PI");
    now += 100001U;
    receive("PING\r\n");
    expect("PING\r\n");
    now = UINT32_MAX - 10U;
    receive("PI");
    now += 100001U;
    receive("PING\r\n");
    expect("PING\r\n");
    for (uint8_t error = 0U; error < 4U; error++)
    {
        receive("PI");
        host_usart.SR = (0x1U << 5) | (0x1U << error);
        host_usart.DR = 'N';
        USART1_IRQHandler();
        receive("G\r\n");
        expect("");
        receive("PING\r\n");
        expect("PING\r\n");
    }
    receive("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n");
    expect("");
    receive("PING\r\n");
    expect("PING\r\n");
    transmitting = true;
    receive("OK,PING,PONG\r\n");
    transmitting = false;
    expect("");
    host_usart.SR = (0x1U << 7) | (0x1U << 6);
    uart1_write_text("OK,");
    assert((host_gpio.MODER & (0x3U << 12)) == 0U);
    uart1_write_text("PING,PONG\r\n");
    assert(host_usart.DR == '\n');
    assert(outgoing_length == 0U && transmitting == false);
    host_usart.SR = 0U;
    uart1_write_text("OK,PING,PONG\r\n");
    assert(uart1_transmit_errors == 1U);
    host_usart.SR = (0x1U << 7) | (0x1U << 6);
    uart1_write_text("OK,PING,PONG\r\n");
    assert(outgoing_length == 0U && transmitting == false);
    puts("USART1 model passed: complete frames, mailbox overflow, byte-gap/wrap, receive errors, echo suppression, bounded TX and recovery.");
}
