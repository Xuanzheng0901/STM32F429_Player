#ifndef PLAYER_SHUFFLE_H
#define PLAYER_SHUFFLE_H
#include <stdint.h>
void player_shuffle_reset(uint32_t total, uint32_t current, uint32_t seed);
uint32_t player_shuffle_next(uint32_t current);
#endif
