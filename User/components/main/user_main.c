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
    HAL_GPIO_WritePin(MAX97220_MUTE_GPIO_Port, MAX97220_MUTE_Pin, GPIO_PIN_SET);
    const player_callbacks_t callbacks = {
        .window = UI_set_music_window,
        .playback = UI_set_player_status,
        .error = UI_set_music_error,
        .media = UI_set_media_state,
    };
    if(!player_init(&callbacks)) {
        LOGE("PLAYER", "task creation failed");
        UI_set_music_error();
    }
}
