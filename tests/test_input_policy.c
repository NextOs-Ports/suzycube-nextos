#include "input_policy.h"
#include "nxinput_core.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static int16_t positive_axis(float value)
{
    return (int16_t)(value * (float)INT16_MAX);
}

static int near(float actual, float expected, float tolerance)
{
    return fabsf(actual - expected) <= tolerance;
}

int main(void)
{
    /* SDL controller triggers use 0 for rest even when the physical mapping
     * is a digital button. This is the ROCKNIX b6/b7 regression case: rest
     * must remain exactly neutral instead of becoming 0.5. */
    assert(nxinput_core_trigger(INT16_MIN,
                                SC_INPUT_TRIGGER_DEADZONE) == 0.0f);
    assert(nxinput_core_trigger(0, SC_INPUT_TRIGGER_DEADZONE) == 0.0f);
    assert(nxinput_core_trigger(positive_axis(0.04f),
                                SC_INPUT_TRIGGER_DEADZONE) == 0.0f);
    assert(near(nxinput_core_trigger(INT16_MAX,
                                     SC_INPUT_TRIGGER_DEADZONE),
                1.0f, 0.00001f));

    nxinput_stick_filter filter = {0};
    float x = 1.0f;
    float y = 1.0f;

    /* A worn stick resting around 0.25 stays neutral. */
    nxinput_core_filter_stick(&filter, positive_axis(0.25f), 0,
                              SC_INPUT_STICK_ENTER_DEADZONE,
                              SC_INPUT_STICK_EXIT_DEADZONE, &x, &y);
    assert(filter.active == 0);
    assert(x == 0.0f && y == 0.0f);

    /* Deliberate movement engages; hysteresis keeps it stable until the stick
     * really returns below the 0.30 release threshold. */
    nxinput_core_filter_stick(&filter, positive_axis(0.41f), 0,
                              SC_INPUT_STICK_ENTER_DEADZONE,
                              SC_INPUT_STICK_EXIT_DEADZONE, &x, &y);
    assert(filter.active == 1 && x > 0.0f && y == 0.0f);
    nxinput_core_filter_stick(&filter, positive_axis(0.31f), 0,
                              SC_INPUT_STICK_ENTER_DEADZONE,
                              SC_INPUT_STICK_EXIT_DEADZONE, &x, &y);
    assert(filter.active == 1 && x > 0.0f);
    nxinput_core_filter_stick(&filter, positive_axis(0.29f), 0,
                              SC_INPUT_STICK_ENTER_DEADZONE,
                              SC_INPUT_STICK_EXIT_DEADZONE, &x, &y);
    assert(filter.active == 0);
    assert(x == 0.0f && y == 0.0f);

    /* The deadzone is radial: a diagonal whose components are each below the
     * engage threshold can still be an intentional movement by magnitude. */
    nxinput_core_filter_stick(&filter, positive_axis(0.30f),
                              positive_axis(0.30f),
                              SC_INPUT_STICK_ENTER_DEADZONE,
                              SC_INPUT_STICK_EXIT_DEADZONE, &x, &y);
    assert(filter.active == 1 && x > 0.0f && y > 0.0f);

    puts("SUZY CUBE INPUT POLICY UNIT: PASS");
    return 0;
}
