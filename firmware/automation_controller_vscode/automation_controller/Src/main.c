#include "app_state.h"
#include "spi2.h"
#include "w5500.h"
#include <stdbool.h>

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

    app_state_run();
    uint8_t version;
    spi2_result_t result = w5500_read_version(&version, 1000000U);
    if (result == SPI2_RESULT_OK)
    {
        w5500_probe_version = version;
        w5500_probe_matched = (version == 0x04U);
    }
    w5500_probe_result = result;
    w5500_probe_done = true;

    while (1)
    {
        app_state_run();
    }
}
