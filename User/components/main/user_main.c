#include "log.h"
#include "UI.h"
#include "es9018.h"
#include "player.h"
#include "main.h"

void app_main(void)
{
    log_init(LOG_INFO);
    UI_init();
    ES9018_init();
    ES9018_set_volume(70);
    const player_callbacks_t callbacks = {
        .window = UI_set_music_window,
        .playback = UI_set_player_status,
        .error = UI_set_music_error,
        .media = UI_set_media_state,
        .notice = UI_show_notice,
        .usb = UI_set_usb_status,
    };
    if(!player_init(&callbacks)) {
        LOGE("PLAYER", "task creation failed");
        UI_set_music_error();
    }
    LOGI("MEM", "RTOS heap: free=%u, min=%u bytes (startup task still active)",
         (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize());
}
