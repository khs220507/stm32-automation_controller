#include "app_state.h"
#include "spi2.h"
#include "uart1.h"

int main(void)
{
    spi2_enable_clocks();
    spi2_pins_init();
    spi2_configure();
    uart1_enable_clocks();
    app_state_init();

    while (1)
    {
        app_state_run();
    }
}
