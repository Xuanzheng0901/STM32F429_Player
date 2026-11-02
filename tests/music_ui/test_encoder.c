#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../User/components/display/lvgl_port/lv_port_encoder.c"

GPIO_TypeDef test_gpioe, test_gpiod, test_gpioc;
struct test_timer { void *id; TimerCallbackFunction_t callback; };
static struct test_timer timer;
struct test_queue { uint8_t items[8]; unsigned read, write; };
static struct test_queue queue;
static unsigned controls[5], control_count, knob_created, registered;
static unsigned volume_count, middle_clicks, middle_longs;
static int volume_direction;
static knob_cb_t rotate_cb;
static void *rotate_arg;
static int32_t knob_count;
static button_cb_t press_cb[2];
static void *press_arg;
static button_event_t button_event;
QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{ assert(count == 8 && size == 1); memset(&queue, 0, sizeof(queue)); return &queue; }
int xQueueSend(QueueHandle_t q, const void *value, unsigned wait)
{ (void)wait; assert(q->write - q->read < 8); q->items[q->write++ % 8] = *(uint8_t *)value; return pdTRUE; }
int xQueueReceive(QueueHandle_t q, void *value, unsigned wait)
{ (void)wait; if(q->read == q->write) return pdFALSE; *(uint8_t *)value = q->items[q->read++ % 8]; return pdTRUE; }
unsigned uxQueueMessagesWaiting(QueueHandle_t q) { return q->write - q->read; }
void vQueueDelete(QueueHandle_t q) { assert(q == &queue); }
TimerHandle_t xTimerCreate(const char *name, unsigned ticks, int repeat, void *id, TimerCallbackFunction_t cb)
{ (void)name; assert(ticks == 10 && repeat); timer.id = id; timer.callback = cb; return &timer; }
int xTimerStart(TimerHandle_t t, unsigned wait) { (void)t; (void)wait; return pdPASS; }
int xTimerStop(TimerHandle_t t, unsigned wait) { (void)t; (void)wait; return pdPASS; }
int xTimerDelete(TimerHandle_t t, unsigned wait) { (void)t; (void)wait; return pdPASS; }
void *pvTimerGetTimerID(TimerHandle_t t) { return t->id; }
knob_handle_t iot_knob_create(const knob_config_t *config) { assert(config); ++knob_created; return (void *)1; }
int32_t iot_knob_delete(knob_handle_t k) { (void)k; return 0; }
int32_t iot_knob_register_cb(knob_handle_t k, knob_event_t e, knob_cb_t cb, void *arg)
{ (void)k; (void)e; rotate_cb = cb; rotate_arg = arg; return 0; }
int32_t iot_knob_get_count_value(knob_handle_t k) { (void)k; return knob_count; }
int iot_button_register_cb(button_handle_t b, button_event_t e, button_event_args_t *args, button_cb_t cb, void *arg)
{ (void)b; (void)args; press_cb[e] = cb; press_arg = arg; ++registered; return 0; }
int iot_button_unregister_cb(button_handle_t b, button_event_t e, button_event_args_t *args)
{ (void)b; (void)e; (void)args; return 0; }
button_event_t iot_button_get_event(button_handle_t b) { (void)b; return button_event; }
static void on_control(unsigned index) { assert(index < 5); controls[control_count++] = index; }
static void on_volume(int direction) { ++volume_count; volume_direction = direction; }
static void on_middle_click(void) { ++middle_clicks; }
static void on_middle_long(void) { ++middle_longs; }
static void scan(unsigned times) { while(times--) timer.callback(&timer); }
static lv_indev_data_t read_input(lv_indev_t *input)
{ lv_indev_data_t data = {0}; encoder_read(input, &data); return data; }
static void key(GPIO_TypeDef *port, unsigned pin, bool pressed)
{ if(pressed) port->unused |= pin; else port->unused &= ~pin; scan(3); }
int main(void)
{
    lv_init(); lv_display_t *display = lv_display_create(360, 184);
    TIM_HandleTypeDef tim = {0}; knob_config_t knob = {.htim = &tim};
    lvgl_port_encoder_cfg_t config = {.disp = display, .encoder_a_b = &knob,
        .encoder_enter = (button_handle_t)1, .control_click = on_control, .volume_step = on_volume,
        .middle_click = on_middle_click, .middle_long_press = on_middle_long};
    lv_indev_t *input = lvgl_port_add_encoder(&config); assert(input);
    assert(knob_created == 1 && registered == 2);
    knob_count = 2; rotate_cb((void *)1, rotate_arg); assert(read_input(input).enc_diff == 2);
    button_event = BUTTON_PRESS_DOWN; press_cb[button_event]((button_handle_t)1, press_arg);
    button_event = BUTTON_PRESS_UP; press_cb[button_event]((button_handle_t)1, press_arg);
    /* 编码器确认短按在两次 LVGL 读取之间完成，也必须保留按下和释放。 */
    lv_indev_data_t data = read_input(input);
    assert(data.state == LV_INDEV_STATE_PRESSED && data.continue_reading);
    assert(read_input(input).state == LV_INDEV_STATE_RELEASED);
    test_gpiod.unused |= BUTTON_SIDE1_Pin; scan(1);
    test_gpiod.unused &= ~BUTTON_SIDE1_Pin; scan(3);
    read_input(input); assert(!volume_count); /* 短抖动被忽略。 */
    key(&test_gpiod, BUTTON_SIDE1_Pin, true); scan(20);
    key(&test_gpiod, BUTTON_SIDE1_Pin, false);
    assert(!volume_count); /* 扫描任务只入队，不调 UI 或音频回调。 */
    assert(read_input(input).enc_diff == 0 && volume_count == 1 && volume_direction == -1);
    read_input(input); assert(volume_count == 1); /* 长按不连发。 */
    key(&test_gpiod, BUTTON_SIDE3_Pin, true); key(&test_gpiod, BUTTON_SIDE3_Pin, false);
    assert(read_input(input).enc_diff == 0 && volume_count == 2 && volume_direction == 1);
    test_gpiod.unused |= BUTTON_SIDE2_Pin; scan(1);
    test_gpiod.unused &= ~BUTTON_SIDE2_Pin; scan(3);
    read_input(input); assert(!middle_clicks && !middle_longs);
    key(&test_gpiod, BUTTON_SIDE2_Pin, true); scan(20);
    read_input(input); assert(!middle_clicks && !middle_longs);
    key(&test_gpiod, BUTTON_SIDE2_Pin, false);
    assert(!middle_clicks); /* 短按在松开消抖后入队，延迟到 LVGL 读取才执行。 */
    assert(read_input(input).enc_diff == 0 && volume_count == 2 && middle_clicks == 1);
    key(&test_gpiod, BUTTON_SIDE2_Pin, true); scan(78);
    read_input(input); assert(middle_longs == 0);
    scan(1); assert(middle_longs == 0);
    read_input(input); assert(middle_longs == 1);
    scan(100); read_input(input); assert(middle_longs == 1);
    key(&test_gpiod, BUTTON_SIDE2_Pin, false); read_input(input);
    assert(middle_clicks == 1 && volume_count == 2); /* 长按松开不再触发短按。 */
    key(&test_gpiod, BUTTON_UP1_Pin, true); key(&test_gpiod, BUTTON_UP1_Pin, false);
    key(&test_gpiod, BUTTON_UP2_Pin, true); key(&test_gpiod, BUTTON_UP2_Pin, false);
    key(&test_gpiod, BUTTON_UP3_Pin, true); key(&test_gpiod, BUTTON_UP3_Pin, false);
    key(&test_gpioc, BUTTON_UP4_Pin, true); key(&test_gpioc, BUTTON_UP4_Pin, false);
    key(&test_gpioc, BUTTON_UP5_Pin, true); key(&test_gpioc, BUTTON_UP5_Pin, false);
    assert(control_count == 0); /* 扫描任务不能调用 UI。 */
    for(unsigned i = 0; i < 5; ++i) read_input(input);
    assert(control_count == 5);
    for(unsigned i = 0; i < 5; ++i) assert(controls[i] == 4 - i);
    assert(lvgl_port_remove_encoder(input) == 0);
    puts("PASS: hardware encoder, buffered confirmation, top controls and debounced side volume keys");
}
