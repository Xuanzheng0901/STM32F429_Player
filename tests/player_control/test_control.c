#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "player.h"
#include "sdcard.h"
#include "audio_player.h"
#include "queue.h"
#include "task.h"

/* Fibers preserve both infinite task loops and allow controlled interleavings. */
struct test_queue { unsigned size; bool pending; unsigned char data[2048]; };
static struct test_queue queues[4];
static unsigned queue_count, critical, selected, focused, elapsed, starts;
static bool inject_browse, playing;
static void *main_fiber;
static music_info_t selected_info;
static audio_state_t state;
static uint32_t audio_elapsed;
struct test_task { void *fiber; TaskFunction_t function; };
static struct test_task tasks[2];
static unsigned task_count;

void test_enter_critical(void) { ++critical; }
void test_exit_critical(void) { assert(critical); --critical; }
QueueHandle_t xQueueCreate(unsigned length, unsigned size)
{
    assert(length == 1 && queue_count < 4 && size <= sizeof(queues[0].data));
    struct test_queue *q = &queues[queue_count++]; q->size = size; return q;
}
int xQueueReceive(QueueHandle_t q, void *out, uint32_t timeout)
{
    assert(!critical);
    if(!q->pending && timeout) SwitchToFiber(main_fiber);
    if(!q->pending) return pdFALSE;
    memcpy(out, q->data, q->size); q->pending = false; return pdTRUE;
}
int xQueuePeek(QueueHandle_t q, void *out, uint32_t timeout)
{ assert(!timeout); if(!q->pending) return pdFALSE; memcpy(out, q->data, q->size); return pdTRUE; }
int xQueueOverwrite(QueueHandle_t q, const void *in)
{ memcpy(q->data, in, q->size); q->pending = true; return pdTRUE; }
int xQueueReset(QueueHandle_t q) { q->pending = false; return pdTRUE; }
void vQueueDelete(QueueHandle_t q) { (void)q; }
static VOID WINAPI task_entry(void *arg)
{ struct test_task *t = arg; t->function(NULL); assert(!"task returned"); }
int xTaskCreate(TaskFunction_t fn, const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *out)
{
    (void)name; (void)stack; (void)arg; (void)priority;
    assert(task_count < 2);
    struct test_task *t = &tasks[task_count++]; t->function = fn;
    t->fiber = CreateFiber(65536, task_entry, t); assert(t->fiber);
    if(out) *out = t;
    return pdPASS;
}
TaskHandle_t xTaskCreateStatic(TaskFunction_t fn, const char *name, unsigned stack, void *arg,
                              unsigned priority, StackType_t *memory, StaticTask_t *storage)
{
    assert(stack == 4096 && memory && storage && !((uintptr_t)memory & 7));
    TaskHandle_t task;
    assert(xTaskCreate(fn, name, stack, arg, priority, &task) == pdPASS);
    return task;
}
void vTaskDelete(TaskHandle_t task) { DeleteFiber(((struct test_task *)task)->fiber); }
int xTaskNotify(TaskHandle_t task, uint32_t bits, eNotifyAction action)
{ assert(task == &tasks[0] && bits == AUDIO_CONTROL_EVENT && action == eSetBits); return pdTRUE; }

FRESULT sdcard_mount_and_scan(void) { return FR_OK; }
void sdcard_unmount(void) {}
bool sdcard_is_removed(void) { return false; }
sdcard_event_t sdcard_poll(void) {
    static bool reported;
    if(reported) return SDCARD_UNCHANGED;
    reported = true;
    return SDCARD_INSERTED;
}
FRESULT sdcard_load_music_window(uint32_t center, music_window_t *window)
{
    assert(center < 40);
    memset(window, 0, sizeof(*window));
    window->total = 40; window->count = 15;
    window->first_index = center > 7 ? center - 7 : 0;
    if(window->first_index > 25) window->first_index = 25;
    for(unsigned i = 0; i < window->count; ++i) {
        unsigned index = window->first_index + i;
        window->items[i].type = index == 2 ? MUSIC_MP3 : index == 3 ? MUSIC_FLAC : MUSIC_WAV;
        window->items[i].path[0] = index + 1;
    }
    if(inject_browse && center == 30) { inject_browse = false; player_request(5, false); }
    return FR_OK;
}
static bool on_window(const music_window_t *window, uint32_t index, bool select)
{
    assert(index >= window->first_index && index < window->first_index + window->count);
    if(select) selected = index;
    focused = index; return true;
}
static bool on_error(void) { assert(state == AUDIO_ERROR); return true; }
static bool on_playback(uint32_t index, const music_info_t *info, uint32_t seconds, bool active)
{ assert(info); selected = index; selected_info = *info; elapsed = seconds; playing = active; return true; }

bool audio_player_start(music_info_t *info)
{
    assert(info->type == MUSIC_WAV || info->type == MUSIC_MP3 || info->type == MUSIC_FLAC);
    ++starts; audio_elapsed = 0;
    if(info->path[0] == 32) { state = AUDIO_ERROR; return false; }
    info->sample_rate_hz = 44100; info->bits_per_sample = 16; info->duration_seconds = 90;
    state = AUDIO_PLAYING; return true;
}
void audio_player_stop(void) { state = AUDIO_STOPPED; }
bool audio_player_toggle(void)
{
    assert(state == AUDIO_PLAYING || state == AUDIO_PAUSED);
    state = state == AUDIO_PLAYING ? AUDIO_PAUSED : AUDIO_PLAYING; return true;
}
audio_state_t audio_player_poll(uint32_t timeout)
{ if(timeout) SwitchToFiber(main_fiber); return state; }
audio_state_t audio_player_state(void) { return state; }
uint32_t audio_player_elapsed(void) { return audio_elapsed; }

static void process_all(void)
{
    for(unsigned i = 0; i < 4; ++i) {
        SwitchToFiber(tasks[1].fiber);
        SwitchToFiber(tasks[0].fiber);
    }
    SwitchToFiber(tasks[1].fiber);
    assert(!critical);
}
int main(int argc, char **argv)
{
    main_fiber = ConvertThreadToFiber(NULL); assert(main_fiber);
    if(argc > 1 && !strcmp(argv[1], "headless")) {
        assert(player_init(NULL)); process_all();
        player_request(1, true); process_all(); assert(state == AUDIO_PLAYING && starts == 1);
        player_request(30, false); process_all(); assert(state == AUDIO_PLAYING && starts == 1);
        player_toggle(); process_all(); assert(state == AUDIO_PAUSED);
        player_toggle(); process_all(); assert(state == AUDIO_PLAYING);
        goto done;
    }
    player_callbacks_t callbacks = {.window = on_window, .playback = on_playback, .error = on_error};
    assert(player_init(&callbacks));
    memset(&callbacks, 0, sizeof(callbacks)); /* 初始化复制接口，不保留调用方栈指针。 */
    process_all();
    assert(selected == 0 && !playing && starts == 0);
    player_request(1, true); player_request(0, false); process_all();
    assert(selected == 1 && focused == 0 && playing && starts == 1);
    assert(selected_info.sample_rate_hz == 44100 && selected_info.duration_seconds == 90);
    player_request(30, false); process_all();
    assert(selected == 1 && focused == 30 && starts == 1);
    player_request(5, false); process_all();
    player_request(30, true); inject_browse = true; process_all();
    assert(selected == 30 && focused == 5 && playing && starts == 2);
    audio_elapsed = 7; player_toggle(); player_set_repeat(true); process_all();
    assert(!playing && state == AUDIO_PAUSED && elapsed == 7);
    player_toggle(); process_all(); assert(playing && elapsed == 7 && starts == 2);
    state = AUDIO_FINISHED; process_all();
    assert(playing && starts == 3 && elapsed == 0);
    player_set_repeat(false); process_all(); state = AUDIO_FINISHED; process_all();
    assert(!playing && starts == 3);
    player_toggle(); process_all(); assert(playing && starts == 4);
    player_request(2, true); process_all();
    assert(selected == 2 && playing && selected_info.type == MUSIC_MP3 && starts == 5);
    player_toggle(); process_all(); assert(!playing && state == AUDIO_PAUSED && starts == 5);
    player_toggle(); process_all(); assert(playing && starts == 5);
    player_set_repeat(true); process_all(); state = AUDIO_FINISHED; process_all();
    assert(selected == 2 && playing && starts == 6 && selected_info.type == MUSIC_MP3);
    player_set_repeat(false); process_all();
    player_request(3, true); process_all();
    assert(selected == 3 && playing && starts == 7 && selected_info.type == MUSIC_FLAC);
    player_toggle(); process_all(); assert(!playing && state == AUDIO_PAUSED);
    player_toggle(); process_all(); assert(playing && starts == 7);
    player_set_repeat(true); process_all(); state = AUDIO_FINISHED; process_all();
    assert(selected == 3 && playing && starts == 8 && selected_info.type == MUSIC_FLAC);
    player_set_repeat(false); process_all();
    player_request(31, true); process_all(); assert(selected == 31 && !playing && state == AUDIO_ERROR);
    player_request(1, true); process_all(); assert(selected == 1 && playing && state == AUDIO_PLAYING);
done:
    for(unsigned i = 0; i < 2; ++i) DeleteFiber(tasks[i].fiber);
    ConvertFiberToThread();
    puts("PASS: selection/browse queues, actual playback state, pause/resume, repeat/EOF, MP3 and error recovery");
    return 0;
}
