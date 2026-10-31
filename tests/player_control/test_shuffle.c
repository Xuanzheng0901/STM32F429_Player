#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player_shuffle.h"
int main(void)
{
    uint8_t seen[1025];
    const uint32_t counts[] = {1,2,3,5,16,31,40,255,256,257,1025};
    for(unsigned n = 0; n < sizeof(counts)/sizeof(counts[0]); ++n) {
        uint32_t total = counts[n];
        for(uint32_t seed = 0; seed < 50; ++seed) {
            uint32_t current = seed % total;
            player_shuffle_reset(total, current, seed);
            memset(seen, 0, sizeof(seen)); seen[current] = 1;
            for(uint32_t i = 1; i < total; ++i) {
                current = player_shuffle_next(current);
                assert(current < total && !seen[current]); seen[current] = 1;
            }
            for(unsigned round = 0; round < 4; ++round) {
                memset(seen, 0, sizeof(seen));
                uint32_t last = current;
                for(uint32_t i = 0; i < total; ++i) {
                    current = player_shuffle_next(current);
                    assert(current < total && !seen[current]); seen[current] = 1;
                    if(!i && total > 1) assert(current != last);
                }
            }
        }
    }
    puts("PASS: complete shuffle rounds, boundaries, non-power-of-two sizes and seeds");
}
