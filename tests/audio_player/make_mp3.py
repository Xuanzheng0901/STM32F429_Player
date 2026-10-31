"""Generate tones, MP3s and independent FFmpeg PCM references in a FAT32 image."""
from pathlib import Path
import math
import struct
import subprocess
import sys
import wave

out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
files, manifest = [], []
cases = [(44100, 2, '128k'), (48000, 2, None), (48000, 1, '96k'),
         (22050, 2, '64k'), (32000, 2, 'vbr'), (24000, 1, '64k'),
         (12000, 1, '24k'), (8000, 1, '16k'), (11025, 1, '24k'),
         (16000, 2, '48k'), (48000, 2, '320k')]
for index, (rate, channels, bitrate) in enumerate(cases):
    pcm = b''.join(struct.pack('<' + 'h' * channels,
        *[int(8000 * math.sin(2 * math.pi * (440 + c * 220) * i / rate)) for c in range(channels)])
        for i in range(rate * 3))
    wav = out / f'tone{index}.wav'
    with wave.open(str(wav), 'wb') as f:
        f.setparams((channels, 2, rate, 0, 'NONE', 'not compressed')); f.writeframes(pcm)
    name = f'TRACK{index}.MP3' if bitrate else f'TRACK{index}.WAV'
    dest = out / name
    if bitrate:
        options = ['-q:a', '4'] if bitrate == 'vbr' else ['-b:a', bitrate]
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(wav), '-codec:a', 'libmp3lame',
                        *options, '-write_xing', '0', '-id3v2_version', '0', str(dest)], check=True)
    else:
        dest.write_bytes(wav.read_bytes())
    channel_options = ['-af', 'pan=stereo|c0=c0|c1=c0'] if channels == 1 else ['-ac', '2']
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(dest), *channel_options, '-f', 's16le',
                    str(out / f'expected{index}.pcm')], check=True)
    data = dest.read_bytes()
    if index == 0:
        # An ID3v2.4 tag larger than the input buffer, including its footer.
        size = 20000
        length = bytes([(size >> n) & 127 for n in [21, 14, 7, 0]])
        data = b'ID3\x04\0\x10' + length + bytes(size) + b'3DI\x04\0\x10' + length + data
    files.append((name, data))
    manifest.append(f'{name} {rate}\n')

# VBR Xing duration is checked independently of encoder delay/padding handling.
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(out / 'tone0.wav'), '-codec:a', 'libmp3lame',
                '-q:a', '4', str(out / 'XING.MP3')], check=True)
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-flags2', '+skip_manual', '-i', str(out / 'XING.MP3'),
                '-f', 's16le', str(out / 'expected11.pcm')], check=True)
manifest.append('XING.MP3 44100\n')
files += [('XING.MP3', (out / 'XING.MP3').read_bytes()),
          ('BROKEN.MP3', b'no MP3 frames here'),
          ('BADTAG.MP3', b'ID3\x04\0\0\x7f\x7f\x7f\x7f'),
          ('GARBAGE.MP3', b'\xaa' * 100000)]
(out / 'manifest.txt').write_text(''.join(manifest), encoding='ascii')

sectors, reserved, fat_sectors = 131072, 32, 1024
boot = bytearray(512); boot[:3] = b'\xeb\x58\x90'; boot[3:11] = b'MSDOS5.0'
struct.pack_into('<HBHBHHBHHHII', boot, 11, 512, 1, reserved, 2, 0, 0, 0xf8, 0, 63, 255, 0, sectors)
struct.pack_into('<IHHIHH', boot, 36, fat_sectors, 0, 0, 2, 1, 6)
boot[64] = 0x80; boot[66] = 0x29; boot[71:82] = b'MP3 TEST   '
boot[82:90] = b'FAT32   '; boot[510:512] = b'\x55\xaa'
fat = bytearray(fat_sectors * 512)
for i, value in enumerate([0x0ffffff8, 0xffffffff, 0x0fffffff]): struct.pack_into('<I', fat, i * 4, value)
root, next_cluster = bytearray(), 3
def cluster_offset(cluster): return (reserved + 2 * fat_sectors + cluster - 2) * 512
with (out / 'card.img').open('wb') as image:
    image.truncate(sectors * 512); image.seek(0); image.write(boot)
    for name, data in files:
        first, count = next_cluster, (len(data) + 511) // 512
        for n in range(count):
            struct.pack_into('<I', fat, (first + n) * 4, first + n + 1 if n + 1 < count else 0x0fffffff)
        next_cluster += count
        base, ext = name.split('.')
        entry = bytearray(32); entry[:11] = (base.ljust(8) + ext.ljust(3)).encode('ascii'); entry[11] = 0x20
        struct.pack_into('<H', entry, 20, first >> 16); struct.pack_into('<H', entry, 26, first & 0xffff)
        struct.pack_into('<I', entry, 28, len(data)); root += entry
        image.seek(cluster_offset(first)); image.write(data)
    image.seek(cluster_offset(2)); image.write(root)
    for n in range(2): image.seek((reserved + n * fat_sectors) * 512); image.write(fat)
print('Created MP3/WAV FAT32 fixtures and independent FFmpeg stereo PCM references.')
