"""Convert an original/high-precision source to the player's native PCM format."""
import argparse
import subprocess
parser = argparse.ArgumentParser(description='Convert to 28800 Hz, mono, dithered 8-bit PCM WAV')
parser.add_argument('source')
parser.add_argument('destination', help='New .wav path; existing files are not overwritten')
args = parser.parse_args()
subprocess.run([
    'ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error', '-n',
    '-i', args.source, '-ac', '1',
    '-af', 'aresample=28800:osf=u8:dither_method=triangular',
    '-ar', '28800', '-c:a', 'pcm_u8', args.destination
], check=True)
