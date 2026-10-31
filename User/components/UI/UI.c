#include "display.h"
#include "UI.h"
#include "player.h"
#include "log.h"
#include "lv_port_button.h"
#include "lv_port_encoder.h"
#include "lv_port_knob.h"
#include "tim.h"

#include "music_player_assets.h"
#include <string.h>

#define PLAYER_TRACK_COUNT 5U
#define PLAYER_TRACK_CENTER (PLAYER_TRACK_COUNT / 2)
static music_info_t current_info;
static uint32_t total_tracks, list_focus;
static int64_t list_first;
static bool library_ready, library_error;
static player_media_t media_state = PLAYER_NO_CARD;

static lv_group_t *player_group;
static lv_obj_t *track_rows[PLAYER_TRACK_COUNT];
static lv_obj_t *track_markers[PLAYER_TRACK_COUNT];
static lv_obj_t *track_names[PLAYER_TRACK_COUNT];
static lv_obj_t *list_count_label;
static lv_obj_t *title_label, *artist_label, *album_label, *status_label, *index_label;
static lv_obj_t *format_label, *audio_label;
static lv_obj_t *elapsed_label, *duration_label, *progress_fill, *progress_knob;
static lv_obj_t *play_icon, *repeat_label;
static uint32_t current_track;
static uint32_t elapsed;
static bool playing;
static bool repeat_one;
static lv_style_t block_style;

/* 所有坐标均为 SSD1685 横屏的原生像素，360 x 184。 */
static lv_obj_t *ui_block(lv_obj_t *screen, int32_t x, int32_t y,
                         int32_t width, int32_t height, bool white)
{
    lv_obj_t *obj = lv_obj_create(screen);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_add_style(obj, &block_style, 0);
    if(white) lv_obj_set_style_bg_color(obj, lv_color_white(), 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *ui_label(lv_obj_t *screen, const char *text, const lv_font_t *font,
                         int32_t x, int32_t y, int32_t width, int32_t height,
                         lv_text_align_t align, bool white)
{
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_remove_style_all(label);
    /* 小字体和黑色文字继承父对象，避免每个标签重复分配样式。 */
    if(font != &fusion_pixel_12_cjk) lv_obj_set_style_text_font(label, font, 0);
    if(white) lv_obj_set_style_text_color(label, lv_color_white(), 0);
    if(align != LV_TEXT_ALIGN_LEFT) lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text_static(label, text);
    lv_obj_set_size(label, width, font->line_height);
    /* 按字体行框垂直居中，字形本身的基线和偏移由 LVGL 处理。 */
    lv_obj_set_pos(label, x, y + (height - font->line_height) / 2);
    return label;
}

static lv_obj_t *ui_image(lv_obj_t *screen, const lv_image_dsc_t *src,
                         int32_t x, int32_t y)
{
    lv_obj_t *image = lv_image_create(screen);
    lv_obj_remove_style_all(image);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(image, src);
    lv_image_set_antialias(image, false);
    lv_obj_set_pos(image, x, y);
    return image;
}

static void ui_time(lv_obj_t *label, uint32_t seconds)
{
    if(seconds >= 3600)
        lv_label_set_text_fmt(label, "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
                              (unsigned long)(seconds / 60 % 60), (unsigned long)(seconds % 60));
    else
        lv_label_set_text_fmt(label, "%02lu:%02lu", (unsigned long)(seconds / 60),
                              (unsigned long)(seconds % 60));
}

static void ui_update_markers(void)
{
    for(uint32_t i = 0; i < PLAYER_TRACK_COUNT; i++) {
        const char *marker = total_tracks && list_first + i == current_track ? (playing ? ">" : "=") : "";
        if(strcmp(lv_label_get_text(track_markers[i]), marker))
            lv_label_set_text_static(track_markers[i], marker);
    }
}

static void ui_update(void)
{
    lv_label_set_text_static(title_label, total_tracks ? current_info.title :
                            library_error ? "加载失败" : library_ready ? "未找到音乐" :
                            media_state == PLAYER_NO_CARD ? "未插卡" : "扫描中");
    lv_label_set_text_static(artist_label, current_info.artist);
    lv_label_set_text_static(album_label, current_info.album);
    const char *format = current_info.type == MUSIC_WAV ? "WAV" :
                         current_info.type == MUSIC_MP3 ? "MP3" :
                         current_info.type == MUSIC_FLAC ? "FLAC" : "--";
    lv_label_set_text_static(format_label, format);
    const uint32_t rate = current_info.sample_rate_hz;
    const unsigned bits = current_info.bits_per_sample;
    if(rate == 0 || bits == 0)
        lv_label_set_text_static(audio_label, "--");
    else if(rate % 1000 / 100)
        lv_label_set_text_fmt(audio_label, "%lu.%luk/%ub", (unsigned long)(rate / 1000),
                              (unsigned long)(rate % 1000 / 100), bits);
    else
        lv_label_set_text_fmt(audio_label, "%luk/%ub", (unsigned long)(rate / 1000), bits);
    lv_label_set_text_static(status_label, library_error ? "加载失败" :
                            !library_ready ? (media_state == PLAYER_NO_CARD ? "未插卡" : "扫描中") : !total_tracks ? "无音乐" :
                            playing ? "正在播放" : "已暂停");
    lv_label_set_text_fmt(index_label, "%lu / %lu", total_tracks ? (unsigned long)current_track + 1 : 0,
                          (unsigned long)total_tracks);
    const uint32_t duration = current_info.duration_seconds;
    if(duration && elapsed > duration) elapsed = duration;
    ui_time(elapsed_label, elapsed);
    ui_time(duration_label, duration);
    const int32_t x = 12 + (duration ? (int32_t)(((uint64_t)204 * elapsed + duration / 2) / duration) : 0);
    lv_obj_set_width(progress_fill, x - 12 + 1);
    lv_obj_set_x(progress_knob, x - 2);
    lv_image_set_src(play_icon, playing ? &music_player_pause : &music_player_play);
    if(repeat_one) lv_obj_remove_flag(repeat_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(repeat_label, LV_OBJ_FLAG_HIDDEN);
    ui_update_markers();
}

bool UI_set_music_window(const music_window_t *window, uint32_t index, bool select)
{
    if(!window || !title_label || window->count > MUSIC_WINDOW_COUNT) return false;
    if(window->total && index >= window->total) return false;
    uint32_t first = index > PLAYER_TRACK_CENTER ? index - PLAYER_TRACK_CENTER : 0;
    uint32_t end = window->total && window->total - index > PLAYER_TRACK_CENTER + 1 ?
                   index + PLAYER_TRACK_CENTER + 1 : window->total;
    if(window->total && (first < window->first_index ||
       (uint64_t)end > (uint64_t)window->first_index + window->count)) return false;
    if(!lvgl_port_lock(portMAX_DELAY)) return false;
    /* 读取 SD 期间用户可能又旋转了编码器，旧的浏览结果不能拉回高亮。 */
    if(!select && library_ready && index != list_focus) {
        lvgl_port_unlock();
        return true;
    }
    bool initial_window = !library_ready;
    bool total_changed = total_tracks != window->total;
    bool update_player = select || initial_window || total_changed || library_error;
    library_ready = true;
    library_error = false;
    total_tracks = window->total;
    /* 高亮固定在中间，首尾不足的行留空；有符号索引允许第一首上方留白。 */
    list_first = (int64_t)index - PLAYER_TRACK_CENTER;
    list_focus = index;
    for(unsigned i = 0; i < PLAYER_TRACK_COUNT; ++i) {
        int64_t song = list_first + i;
        if(song >= 0 && song < total_tracks) {
            lv_obj_remove_flag(track_rows[i], LV_OBJ_FLAG_HIDDEN);
            const music_info_t *item = &window->items[song - window->first_index];
            const char *extension = item->type == MUSIC_WAV ? "wav" : item->type == MUSIC_MP3 ? "mp3" : "flac";
            char name[sizeof(item->title) + 8];
            lv_snprintf(name, sizeof(name), "%s.%s", item->title, extension);
            if(strcmp(lv_label_get_text(track_names[i]), name)) {
                /* 槽位换了歌曲，横向滚动从新标题开头重新开始。 */
                lv_label_set_long_mode(track_names[i], LV_LABEL_LONG_MODE_CLIP);
                lv_label_set_text(track_names[i], name);
            }
            lv_label_long_mode_t mode = i == PLAYER_TRACK_CENTER ? LV_LABEL_LONG_MODE_SCROLL : LV_LABEL_LONG_MODE_CLIP;
            if(lv_label_get_long_mode(track_names[i]) != mode) lv_label_set_long_mode(track_names[i], mode);
        } else lv_obj_add_flag(track_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    if(select && window->total) {
        current_info = window->items[index - window->first_index];
        current_track = index;
        elapsed = 0;
        playing = false; /* 首次发布曲库只选中歌曲，实际状态由音频任务回传。 */
    }
    if(!window->total) {
        current_info = (music_info_t){.type = MUSIC_UNKNOWN};
        current_track = elapsed = 0;
        playing = false;
    }
    if(total_changed) lv_label_set_text_fmt(list_count_label, "%lu", (unsigned long)total_tracks);
    if(total_tracks) lv_group_focus_obj(track_rows[PLAYER_TRACK_CENTER]);
    /* 编码器发送左右键改变歌曲索引，不在控件之间跳转。focus_obj 会清除编辑状态。 */
    lv_group_set_editing(player_group, true);
    /* 浏览焦点只更新右侧列表，按下确认才修改左侧播放器。 */
    if(update_player) ui_update();
    else ui_update_markers();
    lvgl_port_unlock();
    return true;
}

bool UI_set_player_status(uint32_t index, const music_info_t *info,
                          uint32_t elapsed_seconds, bool is_playing)
{
    if(!info || !title_label || !lvgl_port_lock(portMAX_DELAY)) return false;
    if(index >= total_tracks) {
        lvgl_port_unlock();
        return false;
    }
    library_error = false;
    current_info = *info;
    current_track = index;
    elapsed = elapsed_seconds;
    playing = is_playing;
    ui_update();
    lvgl_port_unlock();
    return true;
}

bool UI_set_music_error(void)
{
    if(!title_label || !lvgl_port_lock(portMAX_DELAY)) return false;
    library_error = true;
    ui_update();
    lvgl_port_unlock();
    return true;
}

void UI_set_media_state(player_media_t state)
{
    lvgl_port_lock(portMAX_DELAY);
    media_state = state;
    library_ready = library_error = playing = false;
    current_info = (music_info_t){.type = MUSIC_UNKNOWN};
    total_tracks = current_track = list_focus = elapsed = 0;
    for(unsigned i = 0; i < PLAYER_TRACK_COUNT; ++i)
        lv_obj_add_flag(track_rows[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_static(list_count_label, "0");
    ui_update();
    lvgl_port_unlock();
}

static void ui_action(lv_event_t *event)
{
    const uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if(!total_tracks) return;
    if(code == LV_EVENT_KEY) {
        uint32_t index = list_focus;
        uint32_t key = lv_event_get_key(event);
        if(key == LV_KEY_LEFT && index > 0) --index;
        else if(key == LV_KEY_RIGHT && index < total_tracks - 1) ++index;
        if(index != list_focus) {
            /* 连续旋转按最新逻辑位置累计，不等待 SD 返回或移动控件焦点。 */
            list_focus = index;
            player_request(index, false);
        }
        return;
    }
    if(code != LV_EVENT_CLICKED) return;
    if(action == 4)
    {
        lv_group_focus_obj(track_rows[PLAYER_TRACK_CENTER]);
        lv_group_set_editing(player_group, true);
        return;
    }
    int64_t selected = action == 5 + PLAYER_TRACK_CENTER ? list_focus : list_first + (int64_t)action - 5;
    if(action >= 5 && (selected < 0 || selected >= total_tracks)) return;
    if(action == 0) {
        repeat_one = !repeat_one;
        player_set_repeat(repeat_one);
    }
    else if(action == 2 || (action >= 5 && selected == current_track)) {
        player_toggle();
        return;
    }
    else
    {
        uint32_t index;
        if(action == 1) index = current_track ? current_track - 1 : total_tracks - 1;
        else if(action == 3) index = current_track + 1 == total_tracks ? 0 : current_track + 1;
        else index = (uint32_t)selected;
        list_focus = index;
        player_request(index, true);
        return;
    }
    ui_update();
}
static void ui_encoder_init(lv_group_t *group)
{
    const button_config_t button_config = {0};
    const button_gpio_config_t gpio = {
        .port = KNOB_BUTTON_GPIO_Port, .pin = KNOB_BUTTON_Pin, .active_level = 0,
    };
    button_handle_t button;
    if(iot_button_create_gpio(&button_config, &gpio, &button) != 0)
    {
        LOGE("ENCODER", "Button initialization failed");
        return;
    }
    const knob_config_t knob = {.htim = &htim3, .default_direction = 0};
    const lvgl_port_encoder_cfg_t config = {
        .disp = lv_display_get_default(), .encoder_a_b = &knob, .encoder_enter = button,
    };
    lv_indev_t *indev = lvgl_port_add_encoder(&config);
    if(indev)
        lv_indev_set_group(indev, group);
    else
    {
        iot_button_delete(button);
        LOGE("ENCODER", "Input initialization failed");
    }
}


void UI_init(void)
{
    /* 首帧直接全刷完整播放器，不发送初始化期间的空白画面。 */
    disp_disable_update();
    lv_port_disp_init();
    if(!lvgl_port_lock(portMAX_DELAY)) return;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    ssd1685_set_refresh_mode(SSD1685_REFRESH_PARTIAL);
    const lv_font_t *small = &fusion_pixel_12_cjk;
    lv_obj_set_style_text_font(screen, small, 0);
    lv_obj_set_style_text_align(screen, LV_TEXT_ALIGN_LEFT, 0);
    lv_style_init(&block_style);
    lv_style_set_bg_color(&block_style, lv_color_black());
    lv_style_set_bg_opa(&block_style, LV_OPA_COVER);
    player_group = lv_group_create();
    lv_group_set_wrap(player_group, false);

    /* 顶部状态栏；MUSIC 保留普通文字，不加底框。 */
    ui_label(screen, "MUSIC", small, 12, 5, 46, 17, LV_TEXT_ALIGN_LEFT, false);
    status_label = ui_label(screen, "已暂停", small, 67, 5, 48, 17, LV_TEXT_ALIGN_CENTER, false);
    index_label = ui_label(screen, "0 / 0", small, 129, 5, 72, 17, LV_TEXT_ALIGN_LEFT, false);
    ui_label(screen, "--", small, 293, 5, 18, 17, LV_TEXT_ALIGN_CENTER, false);
    ui_block(screen, 319, 8, 26, 11, false);
    ui_block(screen, 320, 9, 24, 9, true);
    ui_block(screen, 322, 11, 20, 5, false);
    ui_block(screen, 345, 11, 3, 5, false);
    ui_block(screen, 12, 26, 336, 1, false);

    /* 后台索引完成后填入歌曲信息，不启动音频播放。 */
    ui_image(screen, &music_player_record, 14, 35);
    title_label = ui_label(screen, "", &fusion_pixel_12_cjk, 112, 35, 108, 22, LV_TEXT_ALIGN_LEFT, false);
    artist_label = ui_label(screen, "", small, 112, 65, 108, 11, LV_TEXT_ALIGN_LEFT, false);
    album_label = ui_label(screen, "", small, 112, 83, 108, 11, LV_TEXT_ALIGN_LEFT, false);
    ui_block(screen, 112, 103, 30, 13, false);
    format_label = ui_label(screen, "--", small, 112, 103, 30, 13, LV_TEXT_ALIGN_CENTER, true);
    audio_label = ui_label(screen, "--", small, 151, 103, 69, 13, LV_TEXT_ALIGN_LEFT, false);

    const int32_t progress_x = 12;
    ui_block(screen, 12, 129, 205, 1, false);
    progress_fill = ui_block(screen, 12, 128, progress_x - 12 + 1, 3, false);
    progress_knob = ui_block(screen, progress_x - 2, 126, 5, 7, false);
    elapsed_label = ui_label(screen, "00:00", small, 12, 136, 48, 11, LV_TEXT_ALIGN_LEFT, false);
    duration_label = ui_label(screen, "00:00", small, 169, 136, 48, 11, LV_TEXT_ALIGN_RIGHT, false);

    /* 左侧控制图标中心为 (22/68/114/160/206, 165)，等距 46 px。 */
    const lv_image_dsc_t *const controls[] = {
        &music_player_repeat, &music_player_previous, &music_player_pause,
        &music_player_next, &music_player_list,
    };
    for(uintptr_t i = 0; i < 5; i++)
    {
        const int32_t x = i == 2 ? 99 : 12 + (int32_t)i * 46;
        lv_obj_t *control = ui_image(screen, controls[i], x, i == 2 ? 150 : 155);
        if(i == 2) play_icon = control;
        lv_obj_add_flag(control, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(control, ui_action, LV_EVENT_CLICKED, (void *)i);
    }
    repeat_label = ui_label(screen, "1", small, 20, 159, 6, 11, LV_TEXT_ALIGN_CENTER, false);

    /* 列表焦点反白，播放标记独立于焦点；按下选曲后才切换歌曲。 */
    ui_block(screen, 229, 35, 1, 142, false);
    ui_label(screen, "音乐列表", small, 240, 35, 72, 11, LV_TEXT_ALIGN_LEFT, false);
    list_count_label = ui_label(screen, "0", small, 312, 35, 36, 11, LV_TEXT_ALIGN_RIGHT, false);
    ui_block(screen, 240, 49, 108, 1, false);
    for(uintptr_t i = 0; i < PLAYER_TRACK_COUNT; i++)
    {
        const int32_t y = 54 + (int32_t)i * 24;
        lv_obj_t *row = ui_block(screen, 238, y, 110, 23, false);
        track_rows[i] = row;
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_FOCUSED);
        lv_obj_set_style_text_color(row, lv_color_black(), 0);
        lv_obj_set_style_text_color(row, lv_color_white(), LV_STATE_FOCUSED);
        lv_obj_t *name = ui_label(row, "", small, 6, 0, 83, 23, LV_TEXT_ALIGN_LEFT, false);
        lv_obj_set_style_anim_duration(name, lv_anim_speed(20), 0);
        track_names[i] = name;
        lv_obj_t *marker = ui_label(row, "", small, 96, 0, 6, 23, LV_TEXT_ALIGN_CENTER, false);
        track_markers[i] = marker;
        lv_obj_remove_local_style_prop(name, LV_STYLE_TEXT_COLOR, 0);
        lv_obj_remove_local_style_prop(marker, LV_STYLE_TEXT_COLOR, 0);
        lv_obj_add_event_cb(row, ui_action, LV_EVENT_CLICKED, (void *)(i + 5));
        if(i == PLAYER_TRACK_CENTER) {
            lv_obj_add_event_cb(row, ui_action, LV_EVENT_KEY, (void *)(i + 5));
            lv_group_add_obj(player_group, row);
        }
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
    lv_group_set_editing(player_group, true);
    current_info.type = MUSIC_UNKNOWN;
    ui_encoder_init(player_group);
    ui_update();
    disp_enable_update();
    lv_refr_now(NULL);
    lvgl_port_unlock();
}
