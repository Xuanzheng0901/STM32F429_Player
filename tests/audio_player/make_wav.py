"""Build a FAT32 image and WAV fixtures without accessing a real SD card."""
from pathlib import Path
import struct
import sys

out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
def chunk(tag, payload):
    return tag + struct.pack('<I', len(payload)) + payload + b'\0' * (len(payload) & 1)
def wave(channels, rate, frames, junk=False, bits=16):
    pcm = bytearray()
    stereo = bytearray()
    for i in range(frames):
        left = ((i * 53 + 123) % 60001) - 30000
        right = ((i * 79 + 71) % 60001) - 30000 if channels == 2 else left
        pcm += struct.pack('<h', left)
        if channels == 2: pcm += struct.pack('<h', right)
        stereo += struct.pack('<hh', left, right)
    fmt = struct.pack('<HHIIHH', 1, channels, rate, rate * channels * (bits // 8), channels * (bits // 8), bits)
    body = b'WAVE' + (chunk(b'JUNK', b'x') if junk else b'') + chunk(b'fmt ', fmt) + chunk(b'data', pcm)
    return b'RIFF' + struct.pack('<I', len(body)) + body, stereo

files = []
for index, (name, channels, rate, frames, junk) in enumerate([
    ('STEREO.WAV', 2, 44100, 12500, False),
    ('MONO.WAV', 1, 48000, 123, False),
    ('EDGE.WAV', 2, 22050, 8192, False),
    ('ODDJUNK.WAV', 1, 32000, 5000, True),
    ('CHINES~1.WAV', 2, 44100, 6000, False),
]):
    wav, expected = wave(channels, rate, frames, junk)
    (out / name).write_bytes(wav)
    (out / f'expected{index}.pcm').write_bytes(expected)
    files.append((name, wav, '测试音乐.wav' if index == 4 else None))
files += [('BAD24.WAV', wave(2, 44100, 10, bits=24)[0], None),
          ('BROKEN.WAV', b'not a wave file', None), ('README.TXT', b'ignored', None)]

sectors, reserved, fat_sectors = 131072, 32, 1024
boot = bytearray(512)
boot[:3] = b'\xeb\x58\x90'; boot[3:11] = b'MSDOS5.0'
struct.pack_into('<HBHBHHBHHHII', boot, 11, 512, 1, reserved, 2, 0, 0, 0xf8, 0, 63, 255, 0, sectors)
struct.pack_into('<IHHIHH', boot, 36, fat_sectors, 0, 0, 2, 1, 6)
boot[64] = 0x80; boot[66] = 0x29
boot[71:82] = b'WAV TEST   '; boot[82:90] = b'FAT32   '; boot[510:512] = b'\x55\xaa'
fat = bytearray(fat_sectors * 512)
for i, value in enumerate([0x0ffffff8, 0xffffffff, 0x0fffffff]): struct.pack_into('<I', fat, i * 4, value)
root = bytearray()
next_cluster = 3
def cluster_offset(cluster): return (reserved + 2 * fat_sectors + cluster - 2) * 512
with (out / 'card.img').open('wb') as image:
    image.truncate(sectors * 512); image.seek(0); image.write(boot)
    for name, data, long_name in files:
        first = next_cluster; count = (len(data) + 511) // 512
        for n in range(count):
            cluster = first + n
            struct.pack_into('<I', fat, cluster * 4, cluster + 1 if n + 1 < count else 0x0fffffff)
        next_cluster += count
        base, ext = name.split('.')
        alias = (base.ljust(8) + ext.ljust(3)).encode('ascii')
        if long_name:
            checksum = 0
            for b in alias: checksum = (((checksum & 1) << 7) + (checksum >> 1) + b) & 255
            lfn = bytearray(b'\xff' * 32); lfn[0] = 0x41; lfn[11:14] = bytes([15, 0, checksum]); lfn[26:28] = b'\0\0'
            units = list(struct.unpack('<' + 'H' * len(long_name), long_name.encode('utf-16le'))) + [0]
            units += [0xffff] * (13 - len(units))
            for position, unit in zip([1,3,5,7,9,14,16,18,20,22,24,28,30], units): struct.pack_into('<H', lfn, position, unit)
            root += lfn
        entry = bytearray(32); entry[:11] = alias; entry[11] = 0x20
        struct.pack_into('<H', entry, 20, first >> 16); struct.pack_into('<H', entry, 26, first & 0xffff)
        struct.pack_into('<I', entry, 28, len(data)); root += entry
        image.seek(cluster_offset(first)); image.write(data)
    image.seek(cluster_offset(2)); image.write(root)
    for n in range(2): image.seek((reserved + n * fat_sectors) * 512); image.write(fat)
print('Created FAT32 image, 5 playable WAVs, invalid/ignored files, and expected stereo PCM.')

# Separate listening fixture to copy to the physical card (not part of the image).
import math
import wave as wav_module
with wav_module.open(str(out / 'TEST1K.WAV'), 'wb') as test_tone:
    test_tone.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
    test_tone.writeframes(b''.join(struct.pack('<hh', sample, sample)
        for i in range(44100 * 3)
        for sample in [int(8192 * math.sin(2 * math.pi * 1000 * i / 44100))]))
