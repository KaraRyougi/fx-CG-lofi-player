#!/usr/bin/env python3
"""Audio-band SNR of the pulse-width coder on an ideal 1-bit line (the
driver's residual nonlinearity is not modelled here): 512-bit frames at
14.7456 Mbit/s (Pphi x2), 28 800 frames/s. Until 0.9.2 the 256-bit frame
at 7.3728 Mbit/s was tested as well; the 512-bit frame was 5-6 dB quieter
on this ideal line."""
import os, subprocess, tempfile
import numpy as np
root=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def analyse(raw,bitrate,label):
    data=np.fromfile(raw,dtype=np.uint8)
    bits=np.unpackbits(data,bitorder="little").astype(np.float32)
    x=bits-bits.mean(); n=len(x); del bits
    x*=np.hanning(n).astype(np.float32)
    P=np.abs(np.fft.rfft(x))**2; del x
    f=np.fft.rfftfreq(n,1/bitrate)
    band=lambda lo,hi: float(P[(f>=lo)&(f<hi)].sum())
    s=band(990,1010); harm=sum(band(k*1000-10,k*1000+10) for k in range(2,14))
    full=s/0.5**2
    print(f"{label}: tone {10*np.log10(s/full):.1f} dBFS, THD {10*np.log10(harm/s):.1f} dB")
    for lo,hi in [(20,3000),(3000,7000),(7000,10000),(10000,14400),(14400,20000)]:
        print(f"  noise {lo:>5}-{hi:<5} Hz: {10*np.log10((band(lo,hi)-(s if lo<1000<hi else 0))/full):6.1f} dBFS")
    n14=band(20,14400)-s-harm; n7=band(20,7000)-s-harm
    f14=10*np.log10(full/n14); f7=10*np.log10(full/n7)
    print(f"  SNR 20 Hz-14.4 kHz: {10*np.log10(s/n14):.1f} dB ({f14:.1f} dB below full scale)")
    print(f"  SNR 20 Hz-7 kHz:    {10*np.log10(s/n7):.1f} dB ({f7:.1f} dB below full scale)")
    return f14,f7,10*np.log10(harm/s)
with tempfile.TemporaryDirectory() as tmp:
    exe=os.path.join(tmp,"sdm"); raw=os.path.join(tmp,"sdm512.bin")
    subprocess.check_call(["cc","-std=c11","-O2","-Wall","-Wextra","-Werror",os.path.join(root,"tests/test_sdm.c"),"-o",exe,"-lm"])
    subprocess.check_call([exe,raw])
    assert os.path.getsize(raw)==2*28800*64
    f14,f7,thd=analyse(raw,14745600.0,"PWM512 (Pphi 58.9824 MHz)")
assert f14>49 and f7>56 and thd<-48
print("PWM512 SNR PASS")

