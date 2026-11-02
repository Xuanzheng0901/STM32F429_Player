#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ssd1685.h"
#include "ssd1685_internal.h"
#include "display_internal.h"

/* Encode the protocol model's native expected frame as packed GUI input. */
static void encode_i1_test(uint8_t *pixels, const uint8_t *native,
                           unsigned width, unsigned height)
{
    memset(pixels, 0, width*height/8);
    for(unsigned y=0; y<height; ++y)
        for(unsigned x=0; x<width; ++x)
            if(native[x*(height/8)+y/8] & (0x80U>>(y%8)))
                pixels[y*(width/8)+x/8] |= 0x80U>>(x%8);
}

/* Packed content is white with black diagonal/corner pixels; zero padding
 * must be ignored. Build the expected native frame by placing those pixels. */
static void make_i1_fixture(uint8_t *pixels, uint8_t *native,
                            unsigned width, unsigned height, unsigned stride)
{
    memset(pixels, 0, stride * height);
    memset(native, 0xFF, width * height / 8);
    for(unsigned y=0; y<height; ++y)
    {
        memset(pixels+y*stride, 0xFF, width/8);
        unsigned x=y*13%width;
        pixels[y*stride+x/8] &= (uint8_t)~(0x80U>>(x%8));
        native[x*(height/8)+y/8] &= (uint8_t)~(0x80U>>(y%8));
    }
    for(unsigned corner=0; corner<4; ++corner)
    {
        unsigned x=corner&1 ? width-1 : 0;
        unsigned y=corner&2 ? height-1 : 0;
        pixels[y*stride+x/8] &= (uint8_t)~(0x80U>>(x%8));
        native[x*(height/8)+y/8] &= (uint8_t)~(0x80U>>(y%8));
    }
}


static uint8_t ram[2][SSD1685_BUFFER_SIZE], frame[SSD1685_BUFFER_SIZE], old[SSD1685_BUFFER_SIZE];
static uint8_t gui[SSD1685_BUFFER_SIZE];
static unsigned xs, xe, ys, ye, xc, yc, command, update;
static unsigned operations, resets, refreshes, partials, fasts, power_offs, busy_reads;
static unsigned driver_lines, entry, control, resolution, border, sensor, fast_step;
static bool power;
static size_t write_size, write_received;
static unsigned chunk_calls, tail_calls;

void vTaskDelay(uint32_t ticks) { (void)ticks; }
int HAL_GPIO_ReadPin(void *port, int pin)
{
    (void)port; assert(pin == EPD_BUSY_Pin);
    if(busy_reads) { --busy_reads; return GPIO_PIN_SET; }
    return GPIO_PIN_RESET;
}
void HAL_GPIO_WritePin(void *port, int pin, int state)
{ (void)port; assert(pin == DISPLAY_RES_Pin); (void)state; }

void display_tx_cmd(uint8_t cmd, const uint8_t *p, size_t size)
{
    assert(!busy_reads);
    if(command==SSD1685_CMD_WRITE_BW_RAM || command==SSD1685_CMD_WRITE_REFERENCE)
        assert(write_received==write_size);
    ++operations; command=cmd;
    switch(cmd)
    {
        case SSD1685_CMD_SW_RESET:
            assert(size == 0); ++resets; power=false; fast_step=0; busy_reads=2; break;
        case SSD1685_CMD_DRIVER_OUTPUT:
            assert(size == 3 && p[2] == 0); driver_lines=(p[0]|p[1]<<8)+1; break;
        case SSD1685_CMD_DATA_ENTRY: assert(size == 1); entry=p[0]; break;
        case SSD1685_CMD_UPDATE_CONTROL_1:
            assert(size == 2); control=p[0]; resolution=p[1]; break;
        case SSD1685_CMD_RAM_X_WINDOW:
            assert(size == 2); xs=p[0]; xe=p[1]; assert(xs<=xe && xe<23); break;
        case SSD1685_CMD_RAM_Y_WINDOW:
            assert(size == 4); ys=p[0]|p[1]<<8; ye=p[2]|p[3]<<8; assert(ys>=ye && ys<360); break;
        case SSD1685_CMD_RAM_X_COUNTER: assert(size == 1); xc=p[0]; break;
        case SSD1685_CMD_RAM_Y_COUNTER: assert(size == 2); yc=p[0]|p[1]<<8; break;
        case SSD1685_CMD_BORDER_WAVEFORM: assert(size == 1); border=p[0]; break;
        case SSD1685_CMD_TEMPERATURE_SENSOR: assert(size == 1); sensor=p[0]; break;
        case SSD1685_CMD_TEMPERATURE_WRITE:
            assert(fast_step==1 && size==2 && p[0]==0x6E && p[1]==0); fast_step=2; break;
        case SSD1685_CMD_UPDATE_CONTROL_2: assert(size == 1); update=p[0]; break;
        case SSD1685_CMD_WRITE_BW_RAM:
        case SSD1685_CMD_WRITE_REFERENCE:
            assert(size == 0);
            write_size=(xe-xs+1)*(ys-ye+1); write_received=0;
            break;
        case SSD1685_CMD_DEEP_SLEEP: assert(!power && size == 1 && p[0] == 1); break;
        case SSD1685_CMD_ACTIVATE:
            assert(size == 0);
            if(update == 0xB1) { assert(fast_step==0); fast_step=1; }
            else if(update == 0x91) { assert(fast_step==2); fast_step=3; }
            else if(update == 0x03) { assert(power); power=false; ++power_offs; }
            else
            {
                assert(driver_lines==360 && entry==1 && control==0 && resolution==0x40);
                assert(border==5 && sensor==0x80);
                assert(memcmp(ram[0],frame,sizeof(frame))==0);
                if(update == 0x1C)
                {
                    assert(power); ++partials;
                    assert(memcmp(ram[1],old,sizeof(old))==0);
                }
                else
                {
                    assert(update==0xF4 || update==0xC7);
                    assert(xs==0 && xe==22 && ys==359 && ye==0);
                    for(unsigned i=0; i<sizeof(frame); ++i) assert(ram[1][i]==0);
                    if(update==0xC7) { assert(fast_step==3); ++fasts; }
                    power=update==0xF4;
                }
                ++refreshes;
            }
            busy_reads=2;
            break;
        default: assert(!"Unexpected command/custom waveform write");
    }
}

void display_tx_data(const uint8_t *data, size_t size)
{
    assert(!busy_reads); ++operations;
    assert(command==SSD1685_CMD_WRITE_BW_RAM || command==SSD1685_CMD_WRITE_REFERENCE);
    unsigned bank=command==SSD1685_CMD_WRITE_REFERENCE;
    assert(data && size>0 && size<=23*16 && size%23==0);
    assert(write_received+size<=write_size);
    assert(size==(write_size-write_received<23*16 ? write_size-write_received : 23*16));
    write_received+=size; ++chunk_calls;
    if(size<23*16) ++tail_calls;
    for(size_t i=0; i<size; ++i)
    {
        assert(xc>=xs && xc<=xe && yc<=ys && yc>=ye);
        ram[bank][(359-yc)*23+xc]=data[i];
        if(xc==xe) { xc=xs; yc=yc==ye ? ys : yc-1; }
        else ++xc;
    }
}

static void draw(void)
{
    uint8_t source_before[sizeof(gui)];
    memcpy(old,ram[1],sizeof(old));
    encode_i1_test(gui,frame,360,184);
    memcpy(source_before,gui,sizeof(gui));
    ssd1685_draw(gui,45);
    assert(memcmp(gui,source_before,sizeof(gui))==0);
    assert(memcmp(ram[0],frame,sizeof(frame))==0);
    assert(memcmp(ram[1],frame,sizeof(frame))==0);
    assert(!busy_reads);
}

int main(void)
{
    ssd1685_set_refresh_mode(TEST_REFRESH_MODE);
    memset(frame,0xFF,sizeof(frame));
    frame[0]=0x7F; frame[sizeof(frame)-1]=0xFE;
    draw(); /* Cold draw initializes itself and establishes a full base. */
    assert(resets==1 && refreshes==1 && update==0xF4 && power);
    unsigned before=operations;
    draw(); assert(operations==before);
    frame[120*23+7]^=0xAA;
    draw();
#if TEST_REFRESH_MODE == SSD1685_REFRESH_PARTIAL
    assert(resets==1 && update==0x1C && xs==0 && xe==22 && ys==359 && ye==0);
#elif TEST_REFRESH_MODE == SSD1685_REFRESH_FAST
    assert(resets==2 && update==0xC7 && fasts==1 && !power);
#else
    assert(resets==2 && update==0xF4 && !fasts);
#endif
    frame[40*23+2]=0xAA; frame[42*23+3]=0x55;
    draw();
#if TEST_REFRESH_MODE == SSD1685_REFRESH_PARTIAL
    assert(xs==0 && xe==22 && ys==359 && ye==0);
#endif
    for(unsigned i=0; i<12; ++i)
    {
        unsigned index=i==0 ? 0 : i==1 ? sizeof(frame)-1 : 300*23+22;
        frame[index]^=(uint8_t)(1U<<(i%8));
        draw();
    }
#if TEST_REFRESH_MODE == SSD1685_REFRESH_PARTIAL
    assert(resets==1 && partials==14); /* No implicit periodic full refresh. */
#endif
    /* Cross a chunk boundary, end on one, then cover the entire panel. */
    const unsigned row_counts[]={16,17,32,360};
    for(unsigned i=0; i<sizeof(row_counts)/sizeof(row_counts[0]); ++i)
    {
        unsigned first=row_counts[i]==360 ? 0 : 1;
        unsigned last=first+row_counts[i]-1;
        frame[first*23]^=0x80;
        frame[last*23+22]^=0x01;
        unsigned chunks_before=chunk_calls;
        draw();
#if TEST_REFRESH_MODE == SSD1685_REFRESH_PARTIAL
        assert(xs==0 && xe==22 && ys==359 && ye==0);
        assert(chunk_calls-chunks_before==2*((360+15)/16));
#else
        assert(chunk_calls-chunks_before==3*((360+15)/16));
#endif
    }
    before=operations; draw(); assert(operations==before);
    /* Reusing one conversion buffer must not corrupt later comparisons. */
    uint32_t random=0x1685U;
    for(unsigned i=0; i<32; ++i)
    {
        for(unsigned j=0; j<17; ++j)
        {
            random=random*1664525U+1013904223U;
            frame[random%sizeof(frame)]^=(uint8_t)(1U<<((random>>16)%8));
        }
        draw();
        before=operations; draw(); assert(operations==before);
    }
    unsigned before_reset=resets, before_refresh=refreshes;
    ssd1685_draw_full(gui,45); /* Explicit full even for an identical image. */
    assert(resets==before_reset+1 && refreshes==before_refresh+1 && update==0xF4);
    assert(memcmp(ram[1],frame,sizeof(frame))==0 && power);
    ssd1685_deep_sleep(); assert(!power && power_offs==1);
    before_reset=resets; before_refresh=refreshes;
    draw(); assert(resets==before_reset+1 && refreshes==before_refresh+1 && update==0xF4);
    ssd1685_init();
    ssd1685_set_refresh_mode(TEST_REFRESH_MODE); before_reset=resets;
    draw(); assert(resets==before_reset && update==0xF4);
    enum { stride=360/8+3 };
    uint8_t source[stride*184];
    make_i1_fixture(source,frame,360,184,stride);
    memcpy(old,ram[1],sizeof(old));
    ssd1685_draw(source,stride);
    assert(memcmp(ram[0],frame,sizeof(frame))==0);
    assert(memcmp(ram[1],frame,sizeof(frame))==0);
    before=operations;
    ssd1685_draw(source,stride);
    assert(operations==before);
    draw(); assert(operations==before); /* Packed and padded I1 yield the same image. */
    source[0]^=0x80; frame[0]^=0x80;
    before_reset=resets; before_refresh=refreshes;
    ssd1685_draw_full(source,stride);
    assert(resets==before_reset+1 && refreshes==before_refresh+1 && update==0xF4);
    assert(memcmp(ram[0],frame,sizeof(frame))==0 && memcmp(ram[1],frame,sizeof(frame))==0);
    ssd1685_set_refresh_mode(SSD1685_REFRESH_FAST);
    frame[17*23+4]^=0x08; draw();
    assert(update==0xC7 && !power);
    ssd1685_set_refresh_mode(SSD1685_REFRESH_PARTIAL);
    frame[33*23+8]^=0x80; draw();
    assert(update==0xF4 && power);
    frame[359*23+22]^=0x01; draw();
    assert(update==0x1C && power);
    before_reset=resets; before_refresh=refreshes; before=operations;
    ssd1685_request_full_refresh(); assert(operations==before); /* 请求本身不访问硬件。 */
    draw(); assert(resets==before_reset+1 && refreshes==before_refresh+1 && update==0xF4);
    before=operations; draw(); assert(operations==before);
    frame[40*23+8]^=0x01; draw(); assert(update==0x1C); /* 全刷后恢复局刷。 */
    assert(write_received==write_size && chunk_calls>0 && tail_calls>0);
    printf("PASS mode %d: chunk reuse/boundaries/tails, immutable source, exact random-frame diff, init/BUSY, RAM/OLD sync, mode switches, sleep, padded I1\n",TEST_REFRESH_MODE);
}
