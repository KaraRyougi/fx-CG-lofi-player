"""Generate 10 s of 1 kHz: high-precision sine -> resample -> dither -> u8."""
from pathlib import Path
import subprocess
path = Path(__file__).with_name('Tone1000.wav')
subprocess.run([
    'ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error', '-y',
    '-f', 'lavfi', '-i', 'aevalsrc=63/128*sin(2*PI*1000*t):s=96000:d=10',
    '-af', 'afade=t=in:d=0.01,afade=t=out:st=9.99:d=0.01,'
           'aresample=28800:osf=u8:dither_method=triangular',
    '-ac', '1', '-ar', '28800', '-c:a', 'pcm_u8', str(path)
], check=True)
print(path)
