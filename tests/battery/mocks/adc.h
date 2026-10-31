#include <stdint.h>
typedef struct { unsigned dummy; } ADC_HandleTypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_TIMEOUT } HAL_StatusTypeDef;
extern ADC_HandleTypeDef hadc1;
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc);
