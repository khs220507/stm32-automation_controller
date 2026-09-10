#include "app_state.h"
#include "spi2.h"
#include "w5500.h"
#include <stdbool.h>

/* 부팅 1회 진단 결과. done=true 이후 디버거에서 확인한다.
 * version은 result=OK일 때만 유효하며 matched는 기대값 일치 여부다. */
volatile bool w5500_probe_done;
volatile spi2_result_t w5500_probe_result = SPI2_RESULT_NOT_READY;
volatile uint8_t w5500_probe_version;
volatile bool w5500_probe_matched;

int main(void)
{
    spi2_enable_clocks();
    spi2_pins_init();
    spi2_configure();
    app_state_init();

    /* STATE_INIT을 실행해 안전 출력, timebase, UART를 준비한다. */
    app_state_run();
    uint8_t version;
    /* RDY 준비와 읽기에 최대 약 1초 대기. 실기 전 임시 진단 한도다. */
    spi2_result_t result = w5500_read_version(&version, 1000000U);
    if (result == SPI2_RESULT_OK)
    {
        w5500_probe_version = version;
        /* W5500 데이터시트 v1.0.6 p.43: VERSIONR의 기대값 0x04. */
        w5500_probe_matched = (version == 0x04U);
    }
    w5500_probe_result = result;
    w5500_probe_done = true;

    while (1)
    {
        app_state_run();
    }
}
