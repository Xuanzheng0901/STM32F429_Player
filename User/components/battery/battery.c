#include "battery.h"
#include "adc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"

#define BATTERY_PERIOD_MS 30000U
#define BATTERY_REFERENCE_MV 3300U
#define BATTERY_SAMPLES 16U
#define BATTERY_STACK_WORDS 512U

static _Alignas(8) StackType_t s_stack[BATTERY_STACK_WORDS];
static StaticTask_t s_task;
static battery_callback_t s_callback;

/* ADI 典型锂电池 OCV 表，0～100% 每 5% 一个节点，四舍五入至 mV。
 * 来源和负载/充电时的估算局限见 README.md。 */
static const uint16_t s_curve_mv[] = {
    3306, 3675, 3687, 3710, 3741, 3762, 3775,
    3784, 3793, 3806, 3821, 3841, 3884, 3918,
    3945, 3975, 4008, 4045, 4086, 4129, 4177,
};

static uint8_t battery_percent(uint16_t mv)
{
    if(mv <= s_curve_mv[0]) return 0;
    for(unsigned i = 1; i < sizeof(s_curve_mv) / sizeof(s_curve_mv[0]); ++i) {
        if(mv > s_curve_mv[i]) continue;
        uint32_t span = s_curve_mv[i] - s_curve_mv[i - 1];
        return (uint8_t)((i - 1) * 5 + ((mv - s_curve_mv[i - 1]) * 5 + span / 2) / span);
    }
    return 100;
}

static bool battery_read(uint16_t *millivolts)
{
    uint32_t sum = 0;
    for(unsigned i = 0; i < BATTERY_SAMPLES; ++i) {
        HAL_StatusTypeDef status = HAL_ADC_Start(&hadc1);
        if(status == HAL_OK) status = HAL_ADC_PollForConversion(&hadc1, 10);
        if(status == HAL_OK) sum += HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);
        if(status != HAL_OK) return false;
    }
    /* 12 位右对齐，PB1 电压为电池的一半；先累计再换算，减少取整误差。 */
    const uint32_t scale = 4095U * BATTERY_SAMPLES;
    *millivolts = (uint16_t)((sum * BATTERY_REFERENCE_MV * 2U + scale / 2U) / scale);
    return true;
}

static void battery_task(void *argument)
{
    (void)argument;
    TickType_t wake = xTaskGetTickCount();
    for(;;) {
        uint16_t mv;
        if(battery_read(&mv)) s_callback(mv, battery_percent(mv));
        else LOGW("BATTERY", "ADC read failed");
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(BATTERY_PERIOD_MS));
    }
}

bool battery_init(battery_callback_t callback)
{
    s_callback = callback;
    return xTaskCreateStatic(battery_task, "battery", BATTERY_STACK_WORDS, NULL, 1,
                             s_stack, &s_task) != NULL;
}
