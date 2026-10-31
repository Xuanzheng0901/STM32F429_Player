"""Create FLAC/WAV fixtures with independent FFmpeg native-depth PCM references."""
from pathlib import Path
import math
import struct
import subprocess
import argparse
import wave

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--real', action='append', type=Path, default=[])
args = parser.parse_args()
out = args.output; out.mkdir(parents=True, exist_ok=True)
files, manifest = [], []
cases = [('FLAC', 44100, 2, 16, 4096), ('WAV', 48000, 2, 16, 0),
         ('MP3', 44100, 2, 16, 0), ('FLAC', 48000, 1, 24, 4608),
         ('FLAC', 32000, 2, 16, 1024), ('FLAC', 88200, 2, 24, 4096),
         ('FLAC', 96000, 2, 24, 4096), ('FLAC', 22050, 1, 16, 1152),
         ('FLAC', 48000, 2, 16, 4608), ('FLAC', 44100, 2, 24, 4096),
         ('FLAC', 44100, 1, 16, 4096), ('FLAC', 48000, 2, 16, 4096),
         ('FLAC', 44100, 2, 32, 4096), ('FLAC', 96000, 1, 32, 4608),
         ('WAV', 48000, 2, 24, 0), ('WAV', 44100, 1, 24, 0),
         ('WAV', 96000, 2, 32, 0), ('WAV', 32000, 1, 32, 0),
         ('WAV', 44100, 2, 32, 1), ('WAV', 48000, 1, 24, 1),
         ('WAV', 44100, 2, 24, 2), ('FLAC', 48000, 2, 16, 4096),
         ('FLAC', 48000, 2, 32, 4096), ('FLAC', 44100, 2, 32, 4096),
         ('FLAC', 44100, 2, 32, 4096), ('FLAC', 44100, 2, 32, 4096),
         ('FLAC', 44100, 2, 32, 4096), ('FLAC', 44100, 2, 32, 4096),
         ('FLAC', 44100, 2, 32, 4096)]
for index, (ext, rate, channels, bits, block) in enumerate(cases):
    pcm = bytearray()
    seconds = 20 if index == 6 else 3
    for i in range(rate * seconds):
        for channel in range(channels):
            sample = int((1 << (bits - 3)) * math.sin(2 * math.pi * (440 + 211 * channel) * i / rate))
            if index == 11: sample = ((i * 17239 + channel * 7919) % 60001) - 30000
            if bits == 32:
                # 包含正负满幅和不为零的低 16 位，防止误把 24 位容器当 32 位支持。
                sample = [0, 1, -1, (1 << 31) - 1, -(1 << 31), 0x12345678, -0x1234567][(i + channel * 3) % 7] if i < 7 else sample | ((i * 7919 + channel * 53) & 0xffff)
                if index == 25: sample = -2147483647 if channel == 0 else 2147483646
                if index == 26:
                    sample = ((i * 1664525 + channel * 1013904223) ^ (i * 22695477 + 7919)) & 0xffffffff
                    if sample >= 0x80000000: sample -= 0x100000000
                if index == 27: sample = ((i * (53 if channel == 0 else 79) + (123 if channel == 0 else 72)) % 60001) - 30000
                if index == 28: sample &= ~0xffff
            pcm.extend(sample.to_bytes(bits // 8, 'little', signed=True))
    wav = out / f'tone{index}.wav'
    with wave.open(str(wav), 'wb') as f:
        f.setparams((channels, bits // 8, rate, 0, 'NONE', 'not compressed')); f.writeframes(pcm)
    name = f'TRACK{index}.{ext}'
    target = out / name
    if ext == 'FLAC':
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(wav), '-c:a', 'flac', '-compression_level',
                        '0' if index == 10 else '8', '-bits_per_raw_sample', str(bits), '-strict', 'experimental',
                        '-frame_size', str(block), '-ch_mode', str(1 if index >= 25 else index - 21 if index >= 22 else 0 if bits == 32 else -1),
                        '-lpc_type', 'fixed' if index == 27 else 'levinson',
                        '-metadata', 'title=测试歌曲',
                        '-metadata', 'artist=测试艺术家', '-metadata', 'album=测试专辑', str(target)], check=True)
        header = target.read_bytes()[:42]
        assert ((int.from_bytes(header[18:26], 'big') >> 36) & 31) + 1 == bits
    elif ext == 'MP3':
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(wav), '-c:a', 'libmp3lame', '-b:a', '128k',
                        '-write_xing', '0', '-id3v2_version', '0', str(target)], check=True)
    elif block:
        # 构造扩展 PCM，包括 24 位有效数据装入 32 位容器；GUID 必须为整数 PCM。
        container = 32 if block == 2 else bits
        if block == 2: pcm = b''.join(b'\0' + pcm[i:i+3] for i in range(0, len(pcm), 3))
        fmt = struct.pack('<HHIIHHHHI', 0xfffe, channels, rate, rate * channels * (container // 8),
                          channels * (container // 8), container, 22, bits, 3 if channels == 2 else 4)
        fmt += bytes.fromhex('0100000000001000800000aa00389b71')
        body = b'WAVEfmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(pcm)) + pcm
        target.write_bytes(b'RIFF' + struct.pack('<I', len(body)) + body)
    else: target.write_bytes(wav.read_bytes())
    options = ['-af', 'pan=stereo|c0=c0|c1=c0'] if channels == 1 else ['-ac', '2']
    # 部分 FFmpeg 版本把 24-in-32 WAV 误判为旧式浮点格式，改用原始 24 位 WAV 作为参考。
    reference_source = wav if ext == 'WAV' and block == 2 else target
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(reference_source), *options, '-f', 's16le' if bits == 16 else 's32le',
                    str(out / f'expected{index}.pcm')], check=True)
    files.append((name, target.read_bytes())); manifest.append(f'{name} {rate} {seconds} {bits}\n')

base = bytearray(files[0][1])
big = bytearray(base); big[10:12] = b'\xff\xff'
multi = bytearray(base); multi[20] = (multi[20] & ~0x0E) | (5 << 1)
bad_rate = bytearray(base)
value = int.from_bytes(bad_rate[18:26], 'big'); value = (12345 << 44) | (value & ((1 << 44) - 1)); bad_rate[18:26] = value.to_bytes(8, 'big')
position = 4
while True:
    kind = base[position]; size = int.from_bytes(base[position+1:position+4], 'big'); position += 4 + size
    if kind & 0x80: break
corrupt = bytearray(base); corrupt[position + 30] ^= 0x80
float_wav = bytearray((out / 'TRACK16.WAV').read_bytes()); float_wav[20:22] = b'\x03\0'
bad_guid = bytearray((out / 'TRACK18.WAV').read_bytes()); bad_guid[44] = 3
files += [('FLOAT.WAV', float_wav), ('BADGUID.WAV', bad_guid),
          ('BROKEN.FLAC', b'not a native FLAC file'), ('BIGBLOCK.FLAC', big), ('MULTI.FLAC', multi),
          ('BADRATE.FLAC', bad_rate), ('CORRUPT.FLAC', corrupt), ('TRUNC.FLAC', base[:-900])]
for index, source in enumerate(args.real, len(cases)):
    header = source.read_bytes()[:42]
    assert header[:4] == b'fLaC'
    stream = int.from_bytes(header[18:26], 'big')
    rate = stream >> 44; channels = ((stream >> 41) & 7) + 1
    bits = ((stream >> 36) & 31) + 1
    duration = (stream & ((1 << 36) - 1)) // rate
    name = f'REAL{index}.FLAC'
    files.append((name, source.read_bytes())); manifest.append(f'{name} {rate} {duration} {bits}\n')
    options = ['-af', 'pan=stereo|c0=c0|c1=c0'] if channels == 1 else ['-ac', '2']
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(source), *options, '-f', 's16le' if bits == 16 else 's32le',
                    str(out / f'expected{index}.pcm')], check=True)
(out / 'manifest.txt').write_text(''.join(manifest), encoding='ascii')

sectors, reserved, fat_sectors = (262144, 32, 2048) if args.real else (131072, 32, 1024)
boot = bytearray(512); boot[:3] = b'\xeb\x58\x90'; boot[3:11] = b'MSDOS5.0'
struct.pack_into('<HBHBHHBHHHII', boot, 11, 512, 1, reserved, 2, 0, 0, 0xf8, 0, 63, 255, 0, sectors)
struct.pack_into('<IHHIHH', boot, 36, fat_sectors, 0, 0, 2, 1, 6)
boot[64] = 0x80; boot[66] = 0x29; boot[71:82] = b'FLAC TEST  '
boot[82:90] = b'FAT32   '; boot[510:512] = b'\x55\xaa'
fat = bytearray(fat_sectors * 512)
struct.pack_into('<II', fat, 0, 0x0ffffff8, 0xffffffff)
root_slots = sum(1 + (len(name) + 13) // 13 for name, _ in files)
root_count = (root_slots * 32 + 1 + 511) // 512
for n in range(root_count): struct.pack_into('<I', fat, (2+n)*4, 3+n if n+1 < root_count else 0x0fffffff)
root, next_cluster = bytearray(), 2 + root_count
def cluster_offset(cluster): return (reserved + 2 * fat_sectors + cluster - 2) * 512
with (out / 'card.img').open('wb') as image:
    image.truncate(sectors * 512); image.seek(0); image.write(boot)
    for name, data in files:
        first, count = next_cluster, (len(data) + 511) // 512
        for n in range(count): struct.pack_into('<I', fat, (first+n)*4, first+n+1 if n+1 < count else 0x0fffffff)
        next_cluster += count
        assert next_cluster < sectors - reserved - 2 * fat_sectors + 2, 'fixtures exceed FAT32 image capacity'
        base_name, ext = name.split('.')
        alias = (base_name[:8].ljust(8) + ext[:3].ljust(3)).encode('ascii')
        checksum = 0
        for byte in alias: checksum = (((checksum & 1) << 7) + (checksum >> 1) + byte) & 255
        units = [ord(c) for c in name] + [0]
        lfn_count = (len(units) + 12) // 13
        units += [0xffff] * (lfn_count * 13 - len(units))
        for seq in range(lfn_count, 0, -1):
            lfn = bytearray(b'\xff' * 32); lfn[0] = seq | (0x40 if seq == lfn_count else 0)
            lfn[11:14] = bytes([15, 0, checksum]); lfn[26:28] = b'\0\0'
            for offset, unit in zip([1,3,5,7,9,14,16,18,20,22,24,28,30], units[(seq-1)*13:seq*13]):
                struct.pack_into('<H', lfn, offset, unit)
            root += lfn
        entry = bytearray(32); entry[:11] = alias; entry[11] = 0x20
        struct.pack_into('<H', entry, 20, first >> 16); struct.pack_into('<H', entry, 26, first & 0xffff)
        struct.pack_into('<I', entry, 28, len(data)); root += entry
        image.seek(cluster_offset(first)); image.write(data)
    image.seek(cluster_offset(2)); image.write(root)
    for n in range(2): image.seek((reserved + n * fat_sectors) * 512); image.write(fat)
print('Generated FLAC/WAV/MP3 FAT32 image, exact FFmpeg references and invalid/corrupt streams.')
