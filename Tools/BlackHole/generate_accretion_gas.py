"""Deterministic looping data flipbook. No external images; numpy + Pillow."""
from pathlib import Path
import json
import numpy as np
from PIL import Image

root=Path(__file__).resolve().parents[2]
out=root/'SourceArt/BlackHole'
out.mkdir(parents=True,exist_ok=True)
n=128
rng=np.random.default_rng(240104)
fy,fx=np.meshgrid(np.fft.fftfreq(n),np.fft.fftfreq(n),indexing='ij')
white=rng.standard_normal((n,n))
base=np.fft.fft2(white)
yy,xx=np.mgrid[:n,:n]/n
border=np.clip((1-np.abs(xx*2-1))*6,0,1)*np.clip((1-np.abs(yy*2-1))*6,0,1)
atlas=np.zeros((n*4,n*4,4),dtype=np.uint8)
for frame in range(16):
    phase=frame/16
    angle=phase*2*np.pi
    shift=np.exp(-2j*np.pi*(fx*n*phase+fy*n*phase))
    spectrum=np.exp(-((fx/.038)**2+(fy/.075)**2))
    broad=np.fft.ifft2(base*shift*spectrum).real
    broad=(broad-broad.mean())/max(broad.std(),1e-5)
    wisps=.5+.5*np.sin(15*xx+6*yy+1.1*broad+angle)
    density=np.clip((broad+1.0)/3.0,0,1)**1.25*border
    density*=.65+.35*wisps
    data=np.stack((np.clip(.5+.18*broad,0,1),wisps,np.sqrt(density),density),axis=-1)
    atlas[(frame//4)*n:(frame//4+1)*n,(frame%4)*n:(frame%4+1)*n]=(data*255+.5).astype(np.uint8)
path=out/'T_BH_AccretionGas.png'
Image.fromarray(atlas,'RGBA').save(path)
(out/'T_BH_AccretionGas.json').write_text(json.dumps({'seed':240104,'frames':16,'grid':[4,4],'frame_size':128,'padding':'smooth zero-alpha borders','channels':['coarse','wisps','soft detail','density']},indent=2))
print(path)
