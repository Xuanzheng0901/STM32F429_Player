#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display.h"
static unsigned title_updates;
static void track_title_update(lv_obj_t *obj, const char *text);
#define lv_label_set_text_static track_title_update
#include "../../User/components/UI/UI.c"
#undef lv_label_set_text_static

static void track_title_update(lv_obj_t *obj, const char *text)
{
    if(obj == title_label) ++title_updates;
    lv_label_set_text_static(obj, text);
}

TIM_HandleTypeDef htim3;
GPIO_TypeDef test_gpioe;
static bool locked, pending, request_select;
static uint32_t request_index;
static unsigned toggles;
static bool requested_repeat;
static music_window_t window;
static uint8_t frame[DISP_BUF_SIZE];
static lv_indev_t *test_encoder;
static int32_t encoder_delta;
static bool encoder_pressed;
static uint8_t player_pixels[184][28];

bool lvgl_port_lock(uint32_t timeout) { (void)timeout; assert(!locked); locked = true; return true; }
void lvgl_port_unlock(void) { assert(locked); locked = false; }
void disp_disable_update(void) {}
void disp_enable_update(void) {}
void ssd1685_set_refresh_mode(uint8_t mode) { assert(mode == SSD1685_REFRESH_PARTIAL); }
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{ (void)area; (void)pixels; lv_display_flush_ready(display); }
void lv_port_disp_init(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(display, frame, NULL, sizeof(frame), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
}
int iot_button_create_gpio(const button_config_t *config, const button_gpio_config_t *gpio,
                           button_handle_t *button)
{ (void)config; (void)gpio; *button = (button_handle_t)1; return 0; }
int iot_button_delete(button_handle_t button) { (void)button; return 0; }
static void read_encoder(lv_indev_t *encoder, lv_indev_data_t *data)
{
    (void)encoder;
    data->enc_diff = encoder_delta;
    data->state = encoder_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    encoder_delta = 0;
}
lv_indev_t *lvgl_port_add_encoder(const lvgl_port_encoder_cfg_t *config)
{
    lv_indev_t *encoder = lv_indev_create();
    lv_indev_set_type(encoder, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_display(encoder, config->disp);
    lv_indev_set_read_cb(encoder, read_encoder);
    test_encoder = encoder;
    return encoder;
}
void player_request(uint32_t index, bool select)
{ assert(!locked); request_index = index; request_select = select; pending = true; }
void player_toggle(void) { assert(!locked); ++toggles; }
void player_set_repeat(bool repeat) { assert(!locked); requested_repeat = repeat; }

static void publish(uint32_t index, uint32_t total, bool select)
{
    memset(&window, 0, sizeof(window));
    window.total = total;
    window.first_index = index > 7 ? index - 7 : 0;
    uint32_t max_first = total > 15 ? total - 15 : 0;
    if(window.first_index > max_first) window.first_index = max_first;
    window.count = total < 15 ? total : 15;
    for(unsigned i = 0; i < window.count; ++i) {
        snprintf(window.items[i].title, sizeof(window.items[i].title), "song%02lu",
                 (unsigned long)window.first_index + i);
        window.items[i].type = MUSIC_MP3;
    }
    if(!select) list_focus = index; /* 模拟对应浏览请求已在 UI 线程发出。 */
    assert(UI_set_music_window(&window, index, select));
    /* 覆盖调用方缓存，验证 UI 已持有自己的字符串及当前歌曲。 */
    memset(&window, 0xA5, sizeof(window));
    assert(!pending); /* 程序重新定位焦点不会递归产生浏览请求。 */
}

static void process_request(void)
{
    assert(pending);
    pending = false;
    publish(request_index, 42, request_select);
}

static void save_player_pixels(void)
{
    lv_refr_now(NULL);
    for(unsigned y = 0; y < 184; ++y) memcpy(player_pixels[y], frame + 8 + y * 45, 28);
}

static void check_player_pixels(void)
{
    lv_refr_now(NULL);
    for(unsigned y = 0; y < 184; ++y) assert(!memcmp(player_pixels[y], frame + 8 + y * 45, 28));
}

static void check_highlight(void)
{
    lv_refr_now(NULL);
    lv_obj_t *focus = lv_group_get_focused(player_group);
    for(unsigned row = 0; row < PLAYER_TRACK_COUNT; ++row) {
        for(unsigned edge = 0; edge < 2; ++edge) {
            unsigned y = 54 + row * 24 + (edge ? 21 : 1);
            for(unsigned x = 238; x < 348; ++x) {
                bool white = (frame[8 + y * 45 + x / 8] & (0x80U >> (x % 8))) != 0;
                if(white != (focus != track_rows[row])) {
                    fprintf(stderr, "broken highlight row=%u x=%u y=%u white=%d focus=%d\n",
                            row, x, y, white, focus == track_rows[row]);
                    abort();
                }
            }
        }
    }
}

static void check_window(uint32_t index, uint32_t total)
{
    assert(list_focus == index);
    assert(lv_group_get_focused(player_group) == track_rows[PLAYER_TRACK_CENTER]);
    assert(lv_group_get_obj_count(player_group) == 1 && lv_group_get_editing(player_group));
    for(unsigned row = 0; row < PLAYER_TRACK_COUNT; ++row) {
        int64_t song = (int64_t)index + row - PLAYER_TRACK_CENTER;
        bool visible = song >= 0 && song < total;
        assert(lv_obj_has_flag(track_rows[row], LV_OBJ_FLAG_HIDDEN) == !visible);
        if(visible) {
            char title[32];
            snprintf(title, sizeof(title), "song%02lu.mp3", (unsigned long)song);
            assert(!strcmp(lv_label_get_text(track_names[row]), title));
        }
    }
    check_highlight();
}

int main(void)
{
    UI_init();
    assert(!strcmp(lv_label_get_text(status_label), "未插卡"));
    assert(UI_set_music_error());
    assert(!strcmp(lv_label_get_text(title_label), "加载失败"));
    publish(0, 42, true);
    check_window(0, 42);
    assert(!strcmp(lv_label_get_text(status_label), "已暂停"));
    assert(!UI_set_music_window(NULL, 0, false));
    publish(0, 0, true);
    assert(!strcmp(lv_label_get_text(status_label), "无音乐"));
    assert(!strcmp(lv_label_get_text(title_label), "未找到音乐"));
    for(unsigned i = 0; i < 5; ++i) assert(lv_obj_has_flag(track_rows[i], LV_OBJ_FLAG_HIDDEN));
    publish(3, 4, true);
    check_window(3, 4);
    assert(lv_obj_has_flag(track_rows[4], LV_OBJ_FLAG_HIDDEN));
    publish(0, 42, true);
    /* 第一首继续向上不能离开列表，也不需要重新读 SD。 */
    encoder_delta = -20; lv_indev_read(test_encoder);
    assert(!pending); check_window(0, 42);
    assert(UI_set_player_status(current_track, &current_info, 23, true));
    music_info_t selected = current_info;
    unsigned updates = title_updates;
    save_player_pixels();
    for(unsigned index = 1; index < 42; ++index) {
        encoder_delta = 1; lv_indev_read(test_encoder); process_request();
        assert(list_focus == index && current_track == 0);
        assert(playing && elapsed == 23 && !memcmp(&selected, &current_info, sizeof(selected)));
        assert(title_updates == updates);
        check_player_pixels();
        assert(!strcmp(lv_label_get_text(title_label), "song00"));
        check_window(index, 42);
    }
    encoder_delta = 20; lv_indev_read(test_encoder);
    assert(!pending); check_window(41, 42);
    for(unsigned index = 41; index > 0; --index) {
        encoder_delta = -1; lv_indev_read(test_encoder); process_request();
        assert(list_focus == index - 1);
        assert(current_track == 0 && playing && elapsed == 23 && title_updates == updates);
        check_player_pixels();
        check_window(index - 1, 42);
    }
    /* 多个档位在一次读取中累计，SD 未返回前继续旋转也按逻辑索引推进。 */
    encoder_delta = 12; lv_indev_read(test_encoder);
    assert(pending && !request_select && request_index == 12);
    encoder_delta = 15; lv_indev_read(test_encoder);
    assert(request_index == 27 && list_focus == 27);
    encoder_delta = -7; lv_indev_read(test_encoder);
    assert(request_index == 20 && list_focus == 20);
    music_window_t stale = {.total = 42, .first_index = 20, .count = 15};
    assert(UI_set_music_window(&stale, 27, false));
    assert(list_focus == 20 && current_track == 0);
    process_request(); check_window(20, 42); check_player_pixels();
    encoder_delta = 100; lv_indev_read(test_encoder);
    assert(request_index == 41); process_request(); check_window(41, 42);
    encoder_delta = -100; lv_indev_read(test_encoder);
    assert(request_index == 0); process_request(); check_window(0, 42);
    /* 列表尚未返回就按下确认，也应选择快速旋转后的最新位置。 */
    encoder_delta = 20; lv_indev_read(test_encoder);
    assert(pending && request_index == 20 && !request_select);
    encoder_pressed = true; lv_indev_read(test_encoder);
    encoder_pressed = false; lv_indev_read(test_encoder);
    assert(pending && request_select); process_request();
    check_window(20, 42);
    assert(current_track == 20 && !strcmp(lv_label_get_text(title_label), "song20"));
    publish(40, 42, false);
    assert(current_track == 20 && !strcmp(lv_label_get_text(title_label), "song20"));
    assert(UI_set_music_error());
    assert(!strcmp(lv_label_get_text(status_label), "加载失败"));
    assert(!strcmp(lv_label_get_text(title_label), "song20"));
    publish(40, 42, false);
    assert(!strcmp(lv_label_get_text(status_label), "已暂停"));
    assert(UI_set_player_status(current_track, &current_info, 5, false));
    assert(!strcmp(lv_label_get_text(format_label), "MP3"));
    assert(!strcmp(lv_label_get_text(elapsed_label), "00:05"));
    /* 两首长歌名：仅高亮行横向滚动，切换高亮不更换当前曲目。 */
    memset(&window, 0, sizeof(window));
    window.total = window.count = 2;
    strcpy(window.items[0].title, "A very long first song title for scrolling");
    strcpy(window.items[1].title, "A very long second song title for scrolling");
    strcpy(window.items[0].artist, "First artist");
    strcpy(window.items[1].artist, "Second artist");
    window.items[0].type = MUSIC_WAV;
    window.items[1].type = MUSIC_MP3;
    assert(UI_set_music_window(&window, 0, true));
    assert(!strcmp(lv_label_get_text(track_names[2]), "A very long first song title for scrolling.wav"));
    assert(!strcmp(lv_label_get_text(artist_label), "First artist"));
    assert(UI_set_music_window(&window, 0, false));
    lv_obj_update_layout(lv_screen_active());
    assert(lv_label_get_long_mode(track_names[2]) == LV_LABEL_LONG_MODE_SCROLL);
    assert(lv_label_get_long_mode(track_names[3]) == LV_LABEL_LONG_MODE_CLIP);
    assert(lv_anim_get(track_names[2], NULL));
    assert(!lv_anim_get(track_names[3], NULL));
    selected = current_info; updates = title_updates;
    save_player_pixels();
    for(unsigned tick = 0; tick < 6; ++tick) { lv_tick_inc(500); lv_timer_handler(); }
    check_highlight();
    assert(lv_anim_get(track_names[2], NULL)->current_value < 0);
    check_player_pixels();
    encoder_delta = 1; lv_indev_read(test_encoder);
    assert(pending && !request_select && request_index == 1); pending = false;
    assert(UI_set_music_window(&window, 1, false));
    assert(!strcmp(lv_label_get_text(track_names[2]), "A very long second song title for scrolling.mp3"));
    assert(!strcmp(lv_label_get_text(artist_label), "First artist"));
    check_highlight();
    assert(UI_set_music_window(&window, 0, false)); /* 迟到的 SD 结果不能拉回上一行。 */
    assert(list_focus == 1 && lv_group_get_focused(player_group) == track_rows[2]);
    assert(!lv_anim_get(track_names[1], NULL) && lv_anim_get(track_names[2], NULL));
    assert(current_track == 0 && !memcmp(&selected, &current_info, sizeof(selected)) && title_updates == updates);
    check_player_pixels();
    encoder_pressed = true; lv_indev_read(test_encoder);
    encoder_pressed = false; lv_indev_read(test_encoder);
    assert(pending && request_select && request_index == 1); pending = false;
    assert(UI_set_music_window(&window, 1, true));
    assert(current_track == 1 && !strcmp(lv_label_get_text(title_label), window.items[1].title));
    assert(!strcmp(lv_label_get_text(format_label), "MP3"));
    assert(!strcmp(lv_label_get_text(track_markers[1]), "") && !strcmp(lv_label_get_text(track_markers[2]), "="));
    /* 确认仍在后台处理时继续浏览；提交当前曲目不能拉回浏览高亮。 */
    encoder_delta = -1; lv_indev_read(test_encoder); pending = false;
    assert(list_focus == 0);
    assert(UI_set_player_status(1, &window.items[1], 0, false));
    assert(list_focus == 0 && lv_group_get_focused(player_group) == track_rows[2]);
    assert(current_track == 1 && !strcmp(lv_label_get_text(format_label), "MP3"));
    assert(UI_set_music_window(&window, 0, false));
    check_highlight();
    encoder_delta = 1; lv_indev_read(test_encoder); pending = false;
    assert(UI_set_music_window(&window, 1, false));
    for(unsigned step = 0; step < 16; ++step) {
        encoder_delta = step % 2 ? 1 : -1;
        lv_indev_read(test_encoder);
        assert(pending && !request_select); pending = false;
        assert(UI_set_music_window(&window, step % 2 ? 1 : 0, false));
        lv_tick_inc(500); lv_timer_handler();
        check_highlight();
    }
    assert(!locked);
    /* 按键只发命令；播放器收到真实音频快照后再切换状态。 */
    lv_obj_send_event(track_rows[2], LV_EVENT_CLICKED, NULL);
    assert(toggles == 1 && !playing);
    window.items[1].type = MUSIC_WAV;
    window.items[1].sample_rate_hz = 44100;
    window.items[1].bits_per_sample = 16;
    window.items[1].duration_seconds = 90;
    assert(UI_set_player_status(1, &window.items[1], 12, true));
    assert(!strcmp(lv_label_get_text(artist_label), "Second artist"));
    assert(playing && elapsed == 12 && current_track == 1);
    assert(!strcmp(lv_label_get_text(track_markers[2]), ">"));
    lv_obj_send_event(track_rows[2], LV_EVENT_CLICKED, NULL);
    assert(toggles == 2 && playing);
    assert(UI_set_player_status(1, &window.items[1], 12, false));
    assert(!playing && elapsed == 12);
    /* 同名 WAV/MP3 反复切换，艺术家每次覆盖，不能附加或保留旧内容。 */
    for(unsigned i = 0; i < 2; ++i) {
        strcpy(window.items[i].title, "\u7D05\u73AB\u7470(\u570B)");
        strcpy(window.items[i].artist, "Eason Chan");
        window.items[i].type = i ? MUSIC_MP3 : MUSIC_WAV;
    }
    assert(UI_set_music_window(&window, 0, true));
    assert(!strcmp(lv_label_get_text(track_names[2]), "\u7D05\u73AB\u7470(\u570B).wav"));
    assert(!strcmp(lv_label_get_text(track_names[3]), "\u7D05\u73AB\u7470(\u570B).mp3"));
    for(unsigned i = 0; i < 6; ++i) {
        assert(UI_set_player_status(i % 2, &window.items[i % 2], i, true));
        lv_refr_now(NULL);
        assert(!strcmp(lv_label_get_text(title_label), "\u7D05\u73AB\u7470(\u570B)"));
        assert(!strcmp(lv_label_get_text(artist_label), "Eason Chan"));
    }
    UI_set_media_state(PLAYER_NO_CARD);
    assert(!total_tracks && !playing && !library_ready);
    assert(!strcmp(lv_label_get_text(title_label), "未插卡"));
    for(unsigned i = 0; i < PLAYER_TRACK_COUNT; ++i)
        assert(lv_obj_has_flag(track_rows[i], LV_OBJ_FLAG_HIDDEN));
    UI_set_media_state(PLAYER_SCANNING);
    assert(!strcmp(lv_label_get_text(title_label), "扫描中"));
    publish(0, 1, true);
    assert(total_tracks == 1 && !playing && current_track == 0);
    (void)requested_repeat;
    lv_refr_now(NULL);
    puts("PASS: fixed center, boundaries, rapid asynchronous navigation and confirmation; browsing preserves player; scrolling and highlight edges intact");
    return 0;
}
