#include "audio_output.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

#define EAR_INSERTED_LEVEL GPIO_PIN_RESET
#define AMP_MUTED_LEVEL GPIO_PIN_RESET
#define AMP_ENABLED_LEVEL GPIO_PIN_SET
#define EAR_DEBOUNCE_MS 30U

static bool s_inserted, s_candidate;
static uint32_t s_changed;

static bool inserted(void)
{
    return HAL_GPIO_ReadPin(EAR_DET_GPIO_Port, EAR_DET_Pin) == EAR_INSERTED_LEVEL;
}

void audio_output_init(void)
{
    s_inserted = s_candidate = inserted();
    s_changed = HAL_GetTick();
    HAL_GPIO_WritePin(MAX97220_MUTE_GPIO_Port, MAX97220_MUTE_Pin, AMP_MUTED_LEVEL);
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if(pin == EAR_DET_Pin && !inserted())
        HAL_GPIO_WritePin(MAX97220_MUTE_GPIO_Port, MAX97220_MUTE_Pin, AMP_MUTED_LEVEL);
}

int audio_output_poll(void)
{
    bool present = inserted();
    uint32_t now = HAL_GetTick();
    if(present != s_candidate) {
        s_candidate = present;
        s_changed = now;
    }
    if(present == s_inserted || now - s_changed < EAR_DEBOUNCE_MS) return -1;
    s_inserted = present;
    return present;
}

void audio_output_set_active(bool active)
{
    /* 不允许任务中的使能操作覆盖刚到来的拔出中断。 */
    taskENTER_CRITICAL();
    HAL_GPIO_WritePin(MAX97220_MUTE_GPIO_Port, MAX97220_MUTE_Pin,
                     active && s_inserted && inserted() ? AMP_ENABLED_LEVEL : AMP_MUTED_LEVEL);
    taskEXIT_CRITICAL();
}
