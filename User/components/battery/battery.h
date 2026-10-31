#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>
#include <stdint.h>

/* ADC1 已由 MX_ADC1_Init() 初始化；传入有效回调，在低优先级电池任务中执行。 */
typedef void (*battery_callback_t)(uint16_t millivolts, uint8_t percent);
bool battery_init(battery_callback_t callback);

#endif
