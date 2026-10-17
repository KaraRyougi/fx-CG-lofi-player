from pathlib import Path
import subprocess, tempfile, wave, os
import numpy as np
p=Path(__file__).resolve().parents[1]
fixtures=p/'tests/fixtures'
exe=p/'build-host-mp3/mp3-host'
env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1')
def run(source,dest,expected=0,*args):
 r=subprocess.run([str(exe),str(source),str(dest),*map(str,args)],capture_output=True,text=True,env=env,timeout=25)
 assert r.returncode==expected,(r.returncode,r.stdout,r.stderr)
 assert not r.stderr,r.stderr
 print(r.stdout.strip())
with tempfile.TemporaryDirectory() as d:
 d=Path(d)
 for name,rate,channels in [('Test',22050,1),('Stereo',44100,2)]:
  source=fixtures/(name+'.mp3');dest=d/(name+'-28800.wav')
  run(source,dest)
  ref=d/'reference.f32'
  subprocess.run(['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-y','-i',str(source),'-af','anull' if channels==1 else 'pan=mono|c0=0.5*c0+0.5*c1','-f','f32le',str(ref)],check=True)
  values=np.fromfile(ref,'<f4').astype(float)
  with wave.open(str(dest)) as w:
   assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,1,28800)
   actual=np.frombuffer(w.readframes(w.getnframes()),'u1').astype(float)
  assert len(actual)==(len(values)*28800+rate-1)//rate
  positions=np.arange(len(actual))*rate/28800
  # the decoder writes 4 dB below full scale (DECODE_HEADROOM_Q15, src/mp3-decode.h)
  expected=np.clip(np.interp(positions,np.arange(len(values)),values)*20675/32768,-1,1)*128+128
  delta=np.abs(actual-expected)
  assert delta.max()<2.1,(name,delta.max())
  print(name,'FFmpeg decode + independent linear reference: max error',delta.max(),'u8 levels')
  tagged=d/'tagged.mp3';n=100000
  tagged.write_bytes(b'ID3\x04\x00\x00'+bytes((n>>s)&127 for s in [21,14,7,0])+bytes(n)+source.read_bytes())
  output=d/'tagged.wav';run(tagged,output);assert output.read_bytes()==dest.read_bytes()
 source=fixtures/'Test.mp3'
 run(source,d/'unused',3,1)
 run(source,d/'unused',1,0,1)
 original=source.read_bytes()
 for size in [0,1,4,9,10,100,len(original)-100]:
  bad=d/'bad.mp3';bad.write_bytes(original[:size]);run(bad,d/'unused',1)
 bad.write_bytes(b'ID3\x04\x00\x00\x80\x00\x00\x00');run(bad,d/'unused',1)
print('MP3 integration PASS: mono/stereo, resampling, large ID3, cancellation, I/O failure, truncation')
