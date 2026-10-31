#include <assert.h>
#include <stdbool.h>
#include "../../User/components/log/log.c"

UART_HandleTypeDef huart1;
BaseType_t test_yield;
static unsigned notifications;
static bool wake_task;
typedef struct { void *items[LOG_MEM_POOL_SIZE]; unsigned head, size; } mock_queue_t;
static mock_queue_t queues[2];
static unsigned queue_count;
xQueueHandle xQueueCreate(unsigned count, unsigned size)
{
    assert(count == LOG_MEM_POOL_SIZE && size == sizeof(log_data_t *));
    assert(queue_count < 2);
    return &queues[queue_count++];
}
BaseType_t xQueueSend(xQueueHandle handle, const void *item, TickType_t wait)
{
    (void)wait;
    mock_queue_t *queue = handle;
    if(queue->size == LOG_MEM_POOL_SIZE) return pdFALSE;
    queue->items[(queue->head + queue->size++) % LOG_MEM_POOL_SIZE] = *(void *const *)item;
    return pdTRUE;
}
BaseType_t xQueueReceive(xQueueHandle handle, void *item, TickType_t wait)
{
    (void)wait;
    mock_queue_t *queue = handle;
    if(!queue->size) return pdFALSE;
    *(void **)item = queue->items[queue->head];
    queue->head = (queue->head + 1) % LOG_MEM_POOL_SIZE;
    queue->size--;
    return pdTRUE;
}
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *args, unsigned priority, TaskHandle_t *handle)
{
    (void)task; (void)name; (void)args;
    assert(stack == 256 && priority == 12);
    *handle = (TaskHandle_t)1;
    return pdPASS;
}
TickType_t xTaskGetTickCount(void) { return 123; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait)
{ (void)clear; (void)wait; return 1; }
int HAL_UART_Transmit_DMA(UART_HandleTypeDef *uart, const uint8_t *data, uint16_t len)
{ (void)uart; (void)data; (void)len; return HAL_OK; }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken)
{
    assert(task == (TaskHandle_t)1);
    notifications++;
    if(wake_task) *woken = pdTRUE;
}

static void check_format(const char *tag, const char *format, ...)
{
    unsigned char buffer[LOG_RAW_MAX_LEN + 3];
    memset(buffer, 0xa5, sizeof(buffer));
    va_list args;
    va_start(args, format);
    int length = log_format_message((char *)buffer, "32", 'I', 123, tag, format, args);
    va_end(args);
    assert(length > 0 && length <= LOG_RAW_MAX_LEN);
    assert(strlen((char *)buffer) == (size_t)length);
    assert(memcmp(buffer + length - 5, "\033[0m\n", 5) == 0);
    assert(buffer[LOG_RAW_MAX_LEN + 1] == 0xa5);
    assert(buffer[LOG_RAW_MAX_LEN + 2] == 0xa5);
}

int main(void)
{
    char long_text[1024];
    memset(long_text, 'x', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    check_format("TEST", "hello %d", 42);
    check_format("TEST", "%s", long_text);
    check_format(long_text, "hello");
    check_format("", "");
    log_init(LOG_INFO);
    log_data_t *message;
    assert(queues[0].size == LOG_MEM_POOL_SIZE - 1 && queues[1].size == 1);
    assert(xQueueReceive(log_queue, &message, 0) == pdTRUE);
    assert(xQueueSend(log_mem_pool_queue, &message, 0) == pdTRUE);
    log_write(LOG_DEBUG, "TEST", "filtered");
    assert(queues[1].size == 0);
    for(unsigned i = 0; i < LOG_MEM_POOL_SIZE + 1; i++)
        log_write(LOG_INFO, "TEST", "%s", long_text);
    assert(queues[0].size == 0 && queues[1].size == LOG_MEM_POOL_SIZE);
    while(xQueueReceive(log_queue, &message, 0) == pdTRUE) {
        assert(strlen(message->data) == message->len);
        assert(message->len <= LOG_RAW_MAX_LEN);
        assert(xQueueSend(log_mem_pool_queue, &message, 0) == pdTRUE);
    }
    assert(queues[0].size == LOG_MEM_POOL_SIZE);
    wake_task = true;
    HAL_UART_TxCpltCallback(&huart1);
    assert(notifications == 1 && test_yield == pdTRUE);
    wake_task = false;
    HAL_UART_TxCpltCallback(&huart1);
    assert(notifications == 2 && test_yield == pdFALSE);
    UART_HandleTypeDef other = {0};
    HAL_UART_TxCpltCallback(&other);
    assert(notifications == 2);
    puts("log truncation, ANSI suffix and ISR scheduling passed");
}
