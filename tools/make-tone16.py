"""Generate Tone1000-16.wav: 10 s of 1 kHz at -6 dBFS, 16-bit mono 28800 Hz.
Pure Python (no ffmpeg); exact double-precision sine, rounded to int16."""
from pathlib import Path
import math, struct, wave
path = Path(__file__).with_name('Tone1000-16.wav')
rate, seconds, amplitude = 28800, 10, 0.5
n = rate * seconds
frames = bytearray()
for i in range(n):
    fade = min(1.0, i / (rate * 0.01), (n - 1 - i) / (rate * 0.01))
    v = round(32767 * amplitude * fade * math.sin(2 * math.pi * 1000 * i / rate))
    frames += struct.pack('<h', v)
with wave.open(str(path), 'wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(bytes(frames))
print(path)
