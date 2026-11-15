#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../../User/components/battery/battery.c"

ADC_HandleTypeDef hadc1;
static unsigned adc_value, starts, stops, reads, polls, notices, warnings, cycle;
static unsigned fail_start, fail_poll;
static uint16_t last_mv;
static uint8_t last_percent;
static jmp_buf done;
static void (*entry)(void *);

HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc)
{
    assert(adc == &hadc1);
    return ++starts == fail_start ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc)
{ assert(adc == &hadc1); ++stops; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout)
{
    assert(adc == &hadc1 && timeout == 10);
    return ++polls == fail_poll ? HAL_TIMEOUT : HAL_OK;
}
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc)
{ assert(adc == &hadc1); return adc_value + (++reads % 2 ? 0 : 4); }
void battery_warning(void) { ++warnings; }
static void publish(uint16_t mv, uint8_t percent)
{ ++notices; last_mv = mv; last_percent = percent; }
TickType_t xTaskGetTickCount(void) { return 123; }
TaskHandle_t xTaskCreateStatic(void (*task)(void *), const char *name, uint32_t words,
                             void *argument, unsigned priority, StackType_t *stack,
                             StaticTask_t *tcb)
{
    assert(!strcmp(name, "battery") && words == 512 && !argument && priority == 1);
    assert(stack == s_stack && tcb == &s_task);
    entry = task;
    return tcb;
}
void vTaskDelayUntil(TickType_t *wake, TickType_t period)
{
    assert(period == 5000 && *wake == 123 + cycle * period);
    *wake += period;
    if(cycle == 0) {
        assert(notices == 1 && last_mv == 4000 && last_percent == 79);
        fail_poll = polls + 3;
    } else if(cycle == 1) {
        assert(notices == 1 && warnings == 1); /* 失败不发布错误的 0%，下次继续。 */
        adc_value = 2604; /* 平均 2606，对应 4200 mV。 */
    } else {
        assert(notices == 2 && last_mv == 4200 && last_percent == 100);
        longjmp(done, 1);
    }
    ++cycle;
}

int main(void)
{
    const uint16_t knots[] = {3306, 3675, 3687, 3710, 3741, 3762, 3775,
        3784, 3793, 3806, 3821, 3841, 3884, 3918, 3945, 3975, 4008, 4045,
        4086, 4129, 4177};
    for(unsigned i = 0; i < sizeof(knots) / sizeof(knots[0]); ++i)
        assert(battery_percent(knots[i]) == i * 5);
    unsigned previous = 0;
    for(unsigned mv = 0; mv <= 6600; ++mv) {
        unsigned percent = battery_percent((uint16_t)mv);
        assert(percent >= previous && percent <= 100);
        previous = percent;
    }
    uint16_t mv = 999;
    fail_start = 1;
    assert(!battery_read(&mv) && mv == 999 && !reads && !polls && starts == stops);
    fail_start = 0;
    adc_value = 2480; /* 交替 2480/2484，先平均再换算。 */
    assert(battery_init(publish));
    if(!setjmp(done)) entry(NULL);
    assert(starts == stops && warnings == 1);
    puts("PASS: voltage conversion, averaging, OCV interpolation, ADC errors and 5s task recovery");
}
