#include <assert.h>
#include <stdio.h>
#include "lv_port_knob.h"
#include "timers.h"

struct test_timer { TimerCallbackFunction_t callback; };
static struct test_timer timer;
TimerHandle_t xTimerCreate(const char *name, unsigned ticks, int repeat, void *id,
                          TimerCallbackFunction_t callback)
{
    (void)name; (void)id;
    assert(ticks == 5 && repeat);
    timer.callback = callback;
    return &timer;
}
int xTimerStart(TimerHandle_t t, unsigned wait) { (void)t; (void)wait; return pdTRUE; }
int xTimerStop(TimerHandle_t t, unsigned wait) { (void)t; (void)wait; return pdTRUE; }
int xTimerDelete(TimerHandle_t t, unsigned wait) { (void)t; (void)wait; return pdTRUE; }

static void move(TIM_HandleTypeDef *t, int counts)
{
    t->counter = (uint16_t)(t->counter + counts);
    timer.callback(&timer);
}

int main(void)
{
    TIM_HandleTypeDef hardware = {0};
    knob_config_t config = {.htim = &hardware};
    knob_handle_t knob = iot_knob_create(&config);
    assert(knob && hardware.running);
    move(&hardware, -4);
    assert(iot_knob_get_count_value(knob) == -1);
    assert(iot_knob_get_event(knob) == KNOB_LEFT);
    move(&hardware, 4);
    assert(iot_knob_get_count_value(knob) == 0);
    assert(iot_knob_get_event(knob) == KNOB_ZERO);
    move(&hardware, 1);
    move(&hardware, 2);
    assert(iot_knob_get_count_value(knob) == 0);
    move(&hardware, 1);
    assert(iot_knob_get_count_value(knob) == 1);
    move(&hardware, -1);
    move(&hardware, -3);
    assert(iot_knob_get_count_value(knob) == 0);
    for(unsigned i = 0; i < 70000; i++) move(&hardware, 4);
    assert(iot_knob_get_count_value(knob) == 70000);
    assert(iot_knob_delete(knob) == 0 && !hardware.running);

    hardware.counter = 65532;
    knob = iot_knob_create(&config);
    move(&hardware, 4);
    assert(iot_knob_get_count_value(knob) == 1);
    assert(iot_knob_delete(knob) == 0);

    config.default_direction = 1;
    knob = iot_knob_create(&config);
    move(&hardware, 4);
    assert(iot_knob_get_count_value(knob) == -1);
    move(&hardware, -8);
    assert(iot_knob_get_count_value(knob) == 1);
    assert(iot_knob_delete(knob) == 0);
    puts("TIM3 wrap, reverse, detents and signed position passed");
}
