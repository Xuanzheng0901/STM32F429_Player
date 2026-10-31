#include "player_shuffle.h"
#include <stdbool.h>

/* 在 2 的幂大小的域上生成可逆置换，过滤越界值；不保存整张歌单。 */
static uint32_t s_total, s_mask, s_key, s_skip, s_deferred;
static uint64_t s_cursor;
static unsigned s_shift;
static uint32_t random_key(void)
{
    s_key ^= s_key << 13;
    s_key ^= s_key >> 17;
    s_key ^= s_key << 5;
    return s_key;
}
void player_shuffle_reset(uint32_t total, uint32_t current, uint32_t seed)
{
    s_total = total;
    s_mask = total ? total - 1U : 0;
    s_mask |= s_mask >> 1; s_mask |= s_mask >> 2; s_mask |= s_mask >> 4;
    s_mask |= s_mask >> 8; s_mask |= s_mask >> 16;
    unsigned bits = 0;
    for(uint32_t n = s_mask; n; n >>= 1) ++bits;
    s_shift = bits > 1 ? bits / 2 : 1;
    s_key = seed ? seed : 0x9e3779b9U;
    s_cursor = 0;
    s_skip = current; /* 手动起播的当前曲目算本轮第一首。 */
    s_deferred = UINT32_MAX;
}
uint32_t player_shuffle_next(uint32_t current)
{
    if(s_total < 2) return 0;
    if(s_deferred != UINT32_MAX) {
        uint32_t next = s_deferred;
        s_deferred = UINT32_MAX;
        return next;
    }
    bool first = false;
    while(1) {
        if(s_cursor > s_mask) {
            random_key();
            s_cursor = 0;
            s_skip = UINT32_MAX;
            first = true;
        }
        uint32_t next = ((uint32_t)s_cursor++ + s_key) & s_mask;
        next ^= next >> s_shift;
        next = (next * (s_key | 1U)) & s_mask;
        next ^= next >> s_shift;
        next = (next + (s_key >> 16)) & s_mask;
        if(next >= s_total || next == s_skip) continue;
        /* 新一轮首曲若等于上一轮末曲，留到第二首，避免边界连播同曲。 */
        if(first && next == current) {
            s_deferred = next;
            continue;
        }
        return next;
    }
}
