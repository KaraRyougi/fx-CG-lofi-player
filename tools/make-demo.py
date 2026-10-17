"""Generate a quiet melody in float, then dither once to 28800 Hz u8 PCM."""
import math
import struct
import subprocess
from pathlib import Path
rate = 96000
samples = bytearray()
for frequency in (440, 523.25, 659.25, 440):
    for i in range(rate):
        envelope = min(1, i/(rate*.02), (rate-1-i)/(rate*.02))
        samples.extend(struct.pack('<f', 24/128*envelope*math.sin(2*math.pi*frequency*i/rate)))
path = Path(__file__).with_name('Demo.wav')
subprocess.run([
    'ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error', '-y',
    '-f', 'f32le', '-ar', str(rate), '-ac', '1', '-i', 'pipe:0',
    '-af', 'aresample=28800:osf=u8:dither_method=triangular',
    '-ar', '28800', '-c:a', 'pcm_u8', str(path)
], input=samples, check=True)
print(path)
