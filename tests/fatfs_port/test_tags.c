#include "mock_platform.h"
#include "music_tags.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned fail_at;
static void fail_selected_transfer(void)
{
    if(mock_dma_calls == fail_at) mock_mode = MOCK_DMA_ERROR;
}

static void expect_tags(const void *data, UINT size, music_type_t type,
                        const char *title, const char *artist, const char *album)
{
    FIL file;
    UINT written;
    music_info_t info = {.type = type, .path = "0:/tag.bin", .title = "fallback", .artist = "fallback"};
    assert(f_open(&file, info.path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_write(&file, data, size, &written) == FR_OK && written == size);
    assert(f_close(&file) == FR_OK);
    assert(music_tags_read(&info) == FR_OK);
    assert(strcmp(info.title, title) == 0);
    assert(strcmp(info.artist, artist) == 0);
    assert(strcmp(info.album, album) == 0);
}

int main(void)
{
    uint8_t work[4096];
    FATFS filesystem;
    MKFS_PARM options = {.fmt = FM_FAT, .au_size = 512};
    assert(f_mkfs("0:", &options, work, sizeof(work)) == FR_OK);
    assert(f_mount(&filesystem, "0:", 1) == FR_OK);

    /* ID3v2.2 短字段名；ID3v2.3 混合文本编码；ID3v2.4 帧级 unsync。 */
    static const uint8_t id3v22[] = "ID3\x02\0\0\0\0\0\x18"
        "TT2\0\0\x02\0T" "TP1\0\0\x02\0A" "TAL\0\0\x02\0B";
    expect_tags(id3v22, sizeof(id3v22) - 1, MUSIC_MP3, "T", "A", "B");
    static const uint8_t id3v23[] = "ID3\x03\0\0\0\0\0\x2a"
        "TIT2\0\0\0\x05\0\0\x02\xd8\x3d\xde\0"
        "TPE1\0\0\0\x05\0\0\x01\xff\xfe\xf3\x97"
        "TALB\0\0\0\x02\0\0\x03Z";
    expect_tags(id3v23, sizeof(id3v23) - 1, MUSIC_MP3, "\xf0\x9f\x98\x80", "\xe9\x9f\xb3", "Z");
    static const uint8_t id3v24[] = "ID3\x04\0\0\0\0\0\x1b"
        "TIT2\0\0\0\x05\0\x02\0X\xff\0\xe1"
        "TPE1\0\0\0\x02\0\0\x03Y";
    expect_tags(id3v24, sizeof(id3v24) - 1, MUSIC_MP3, "X\xc3\xbf\xc3\xa1", "Y", "");

    /* LIST INFO 包含奇数长度填充、未知字段、UTF-8、CP936 和 UTF-16BE。 */
    static const uint8_t wav[] = "RIFF\x42\0\0\0WAVE"
        "LIST\x36\0\0\0INFO"
        "JUNK\x01\0\0\0X\0"
        "INAM\x03\0\0\0Hi\0\0"
        "IART\x03\0\0\0\xd2\xf4\0\0"
        "IPRD\x08\0\0\0\xfe\xff\xd8\x3d\xde\0\0\0";
    expect_tags(wav, sizeof(wav) - 1, MUSIC_WAV, "Hi", "\xe9\x9f\xb3", "\xf0\x9f\x98\x80");

    /* Vorbis 字段大小写混用，未知字段跳过，末尾块标志结束遍历。 */
    static const uint8_t flac[] = "fLaC\0\0\0\0\x84\0\0\x35"
        "\0\0\0\0\x04\0\0\0"
        "\x07\0\0\0title=T"
        "\x08\0\0\0ArTiSt=A"
        "\x07\0\0\0ALBUM=B"
        "\x07\0\0\0OTHER=Z";
    expect_tags(flac, sizeof(flac) - 1, MUSIC_FLAC, "T", "A", "B");

    /* 非文本帧、越界长度和截断标签保留文件名回退，不阻止播放。 */
    uint8_t damaged[sizeof(id3v23) - 1];
    memcpy(damaged, id3v23, sizeof(damaged));
    damaged[19] = 0x80; /* v2.3 压缩帧 */
    expect_tags(damaged, sizeof(damaged), MUSIC_MP3, "fallback", "\xe9\x9f\xb3", "Z");
    damaged[9] = 0x7f;
    expect_tags(damaged, sizeof(damaged), MUSIC_MP3, "fallback", "fallback", "");
    expect_tags(wav, sizeof(wav) - 2, MUSIC_WAV, "fallback", "fallback", "");
    expect_tags(flac, sizeof(flac) - 2, MUSIC_FLAC, "fallback", "fallback", "");

    /* 文件中即使还有数据，也不允许字段越过所属容器片段。 */
    memcpy(damaged, id3v23, sizeof(damaged));
    damaged[9] = 14; /* 第一帧头完整，但帧正文超出 ID3 标签范围。 */
    expect_tags(damaged, sizeof(damaged), MUSIC_MP3, "fallback", "fallback", "");
    uint8_t clipped[sizeof(wav) - 1];
    memcpy(clipped, wav, sizeof(clipped));
    clipped[4] = 4; /* 后续 LIST 位于 RIFF 声明的范围之外。 */
    expect_tags(clipped, sizeof(clipped), MUSIC_WAV, "fallback", "fallback", "");
    memcpy(clipped, wav, sizeof(clipped));
    clipped[16] = 4; /* INFO 只有类型头，INAM 等字段不属于此 LIST。 */
    expect_tags(clipped, sizeof(clipped), MUSIC_WAV, "fallback", "fallback", "");

    /* 实际 ID3 文件中的非法 UTF-8 不覆盖回退标题。 */
    static const uint8_t invalid_utf8[] = "ID3\x03\0\0\0\0\0\x0d"
        "TIT2\0\0\0\x03\0\0\x03\xc0\xaf";
    expect_tags(invalid_utf8, sizeof(invalid_utf8) - 1, MUSIC_MP3, "fallback", "fallback", "");

    /* 标签文本正好在四字节 UTF-8 字符中截断。 */
    uint8_t long_tag[88] = "ID3\x03\0\0\0\0\0\x4e"
        "TIT2\0\0\0\x44\0\0\x03";
    memset(long_tag + 21, 'A', 63);
    memcpy(long_tag + 84, "\xf0\x9f\x98\x80", 4);
    char expected[64];
    memset(expected, 'A', 63);
    expected[63] = 0;
    expect_tags(long_tag, sizeof(long_tag), MUSIC_MP3, expected, "fallback", "");

    /* ID3v1 的三个字段直接从已读入的尾部解码，非空 ID3v2 字段仍优先。 */
    uint8_t combined[256] = {0};
    memcpy(combined, id3v24, sizeof(id3v24) - 1);
    memcpy(combined + 128, "TAGold title", 12);
    memcpy(combined + 161, "old artist", 10);
    memcpy(combined + 191, "old album", 9);
    expect_tags(combined + 128, 128, MUSIC_MP3, "old title", "old artist", "old album");
    expect_tags(combined, sizeof(combined), MUSIC_MP3, "X\xc3\xbf\xc3\xa1", "Y", "old album");

    /* 257 个空块之后的有效标签仍应被读取，遍历由容器长度约束。 */
    uint8_t many_chunks[12 + 257 * 8 + 22] = "RIFF\0\0\0\0WAVE";
    uint32_t riff_size = sizeof(many_chunks) - 8;
    for(unsigned i = 0; i < 4; ++i) many_chunks[4 + i] = (uint8_t)(riff_size >> (i * 8));
    for(unsigned i = 0; i < 257; ++i) memcpy(many_chunks + 12 + i * 8, "JUNK", 4);
    memcpy(many_chunks + 12 + 257 * 8, "LIST\x0e\0\0\0INFOINAM\x02\0\0\0Q\0", 22);
    expect_tags(many_chunks, sizeof(many_chunks), MUSIC_WAV, "Q", "fallback", "");

    /* 文本跨扇区，逐个 DMA 注入错误，覆盖打开文件及部分字段读取后的失败。 */
    uint8_t large_tag[2048] = "ID3\x03\0\0\0\0\x0f\x76"
        "TIT2\0\0\0\x02\0\0\x03T" "APIC\0\0\x06\x40\0\0";
    memcpy(large_tag + 1632, "TPE1\0\0\0\x02\0\0\x03" "A" "TALB\0\0\0\x02\0\0\x03" "B", 24);
    expect_tags(large_tag, sizeof(large_tag), MUSIC_MP3, "T", "A", "B");
    music_info_t info = {.type = MUSIC_MP3, .path = "0:/tag.bin"};
    assert(f_mount(&filesystem, "0:", 1) == FR_OK);
    mock_clear_counters();
    assert(music_tags_read(&info) == FR_OK);
    unsigned transfers = mock_dma_calls;
    assert(transfers >= 3);
    for(fail_at = 1; fail_at <= transfers; ++fail_at) {
        assert(f_mount(&filesystem, "0:", 1) == FR_OK);
        mock_clear_counters();
        mock_dma_hook = fail_selected_transfer;
        assert(music_tags_read(&info) == FR_DISK_ERR);
        mock_dma_hook = NULL;
        mock_mode = MOCK_NORMAL;
    }
    assert(f_mount(&filesystem, "0:", 1) == FR_OK);
    assert(music_tags_read(&info) == FR_OK);
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    puts("ID3, WAV INFO, Vorbis comments, text encodings and malformed tags passed");
}
