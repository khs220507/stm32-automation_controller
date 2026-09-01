#include "app_state.h"

#include <stdint.h>

#include "board_io.h"
#include "hcsr04.h"
#include "protocol.h"
#include "timebase.h"
#include "uart2.h"

#define HCSR04_MEASUREMENT_PERIOD_US 100000U

typedef enum
{
    STATE_INIT = 0,
    STATE_IDLE,
    STATE_AUTO,
    STATE_DIAG,
    STATE_STOP,
    STATE_FAULT
} system_state_t;

static system_state_t current_state;
static hcsr04_measurement_t latest_hcsr04_measurement;
static uint32_t hcsr04_last_measurement_time_us;

static const char *app_state_name(system_state_t state);
static void app_state_run_auto(protocol_command_t command);
static void app_state_clear_measurement(void);

void app_state_init(void)
{
    current_state = STATE_INIT;
    app_state_clear_measurement();
    hcsr04_last_measurement_time_us = 0U;
}

void app_state_run(void)
{
    protocol_command_t command;

    if (current_state == STATE_INIT)
    {
        board_io_init();
        timebase_init();
        hcsr04_init();
        uart2_init();
        protocol_init();
        protocol_send_ready();
        current_state = STATE_IDLE;
        return;
    }

    command = protocol_poll_command();
    if (command == PROTOCOL_COMMAND_GET_STATUS)
    {
        protocol_send_status(app_state_name(current_state));
        command = PROTOCOL_COMMAND_NONE;
    }

    switch (current_state)
    {
    case STATE_IDLE:
        if (command == PROTOCOL_COMMAND_START)
        {
            hcsr04_last_measurement_time_us =
                timebase_now_us() - HCSR04_MEASUREMENT_PERIOD_US;
            current_state = STATE_AUTO;
            protocol_send_state("AUTO");
        }
        break;

    case STATE_AUTO:
        app_state_run_auto(command);
        break;

    case STATE_DIAG:
        break;

    case STATE_STOP:
        board_io_set_safe_outputs();
        app_state_clear_measurement();
        current_state = STATE_IDLE;
        protocol_send_state("IDLE");
        break;

    case STATE_FAULT:
        board_io_set_safe_outputs();
        break;

    case STATE_INIT:
    default:
        board_io_set_safe_outputs();
        current_state = STATE_FAULT;
        break;
    }
}

static void app_state_run_auto(protocol_command_t command)
{
    if (command == PROTOCOL_COMMAND_STOP)
    {
        current_state = STATE_STOP;
        protocol_send_state("STOP");
        return;
    }

    if (timebase_elapsed_us(hcsr04_last_measurement_time_us) <
        HCSR04_MEASUREMENT_PERIOD_US)
    {
        return;
    }

    hcsr04_last_measurement_time_us = timebase_now_us();
    hcsr04_measure(&latest_hcsr04_measurement);

    switch (latest_hcsr04_measurement.status)
    {
    case HCSR04_STATUS_OK:
        protocol_send_hcsr04_ok(latest_hcsr04_measurement.distance_cm,
                                latest_hcsr04_measurement.pulse_us);
        break;

    case HCSR04_STATUS_OUT_OF_RANGE:
        protocol_send_hcsr04_out_of_range(latest_hcsr04_measurement.pulse_us);
        break;

    case HCSR04_STATUS_TIMEOUT:
    default:
        protocol_send_hcsr04_timeout();
        break;
    }
}

static const char *app_state_name(system_state_t state)
{
    switch (state)
    {
    case STATE_INIT:
        return "INIT";
    case STATE_IDLE:
        return "IDLE";
    case STATE_AUTO:
        return "AUTO";
    case STATE_DIAG:
        return "DIAG";
    case STATE_STOP:
        return "STOP";
    case STATE_FAULT:
    default:
        return "FAULT";
    }
}

static void app_state_clear_measurement(void)
{
    latest_hcsr04_measurement.status = HCSR04_STATUS_TIMEOUT;
    latest_hcsr04_measurement.distance_cm = 0U;
    latest_hcsr04_measurement.pulse_us = 0U;
}
