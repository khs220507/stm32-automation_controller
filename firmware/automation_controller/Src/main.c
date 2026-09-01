#include "app_state.h"

int main(void)
{
    app_state_init();

    while (1)
    {
        app_state_run();
    }
}
