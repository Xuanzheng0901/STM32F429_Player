#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <stdbool.h>

void audio_output_init(void);
/* -1 无变化，0 拔出，1 插入。拔出在 EXTI 中先静音，任务只做消抖和通知。 */
int audio_output_poll(void);
void audio_output_set_active(bool active);

#endif
