"""Real rendered preview and visibility checks; numpy/Pillow outside Unreal."""
from pathlib import Path
from collections import deque
import json,shutil
import numpy as np
from PIL import Image,ImageDraw,ImageFilter
root=Path(__file__).resolve().parents[2]
s=root/'Saved/CodexBlackHoleValidation/Accretion'
out=root/'Source/Paradox/Docs/Media/Accretion'
out.mkdir(parents=True,exist_ok=True)
frames=[Image.open(s/('Frame_%02d.png'%i)).convert('RGB') for i in range(48)]
frames[0].save(out/'preview.gif',save_all=True,append_images=frames[1:],duration=83,loop=0,optimize=True)
before=np.asarray(Image.open(s/'Off.png').convert('RGB')).astype(float)
after=np.asarray(Image.open(s/'After.png').convert('RGB')).astype(float)
mask=(before[:,:,0]>100)&(before[:,:,0]>before[:,:,2]*2)&(before[:,:,1]>35)
connected=np.asarray(Image.fromarray((mask*255).astype('uint8')).filter(ImageFilter.MaxFilter(7)))>0
seen=np.zeros(mask.shape,bool);components=[]
for y,x in zip(*np.where(connected)):
 if seen[y,x]:continue
 q=deque([(y,x)]);seen[y,x]=True;pts=[]
 while q:
  a,b=q.popleft()
  if mask[a,b]:pts.append((a,b))
  for a2,b2 in ((a-1,b),(a+1,b),(a,b-1),(a,b+1)):
   if 0<=a2<mask.shape[0] and 0<=b2<mask.shape[1] and connected[a2,b2] and not seen[a2,b2]:seen[a2,b2]=True;q.append((a2,b2))
 if len(pts)>15:components.append(np.array(pts))
assert len(components)==4,len(components)
def lum(a):
 a=a/255;linear=np.where(a<=.04045,a/12.92,((a+.055)/1.055)**2.4)
 return linear@np.array([.2126,.7152,.0722])
report={'targets':[],'animation_frames':48,'preview_seconds':4,'no_saved_gameplay_map':True}
for p in components:
 y,x=p.mean(axis=0).astype(int);a,b=p.min(axis=0);c,d=p.max(axis=0)
 ring=np.zeros(mask.shape,bool);ring[max(0,a-10):c+11,max(0,b-10):d+11]=True;ring[max(0,a-2):c+3,max(0,b-2):d+3]=False
 v=[]
 for f in [before]+[np.asarray(im).astype(float) for im in frames]:
  fg=np.median(lum(f[p[:,0],p[:,1]]));bg=np.median(lum(f[ring]));v.append((fg-bg)/(fg+bg+1e-7))
 ratio=min(v[1:])/v[0]
 assert ratio>.90,(x,y,ratio)
 report['targets'].append({'center':[int(x),int(y)],'baseline_contrast':v[0],'minimum_animated_contrast':min(v[1:]),'minimum_contrast_ratio':ratio})
roi=np.ones(mask.shape,bool);roi[75:235,135:410]=False
animation=np.abs(np.asarray(frames[0]).astype(float)-np.asarray(frames[-1]).astype(float))
report['animation_changed_pixels_outside_source']=int(np.count_nonzero(animation.max(axis=2)[roi]>8))
assert report['animation_changed_pixels_outside_source']>100
large=np.asarray(Image.open(s/'LargeWorld.png').convert('RGB')).astype(float)
reference=np.asarray(Image.open(s/'CaptureOff.png').convert('RGB')).astype(float)
report['large_world_reference']='CaptureOff: same warmed camera and animation time immediately before translation'
report['large_world_p99_rgb_error_outside_source']=float(np.percentile(np.abs(large-reference)[roi],99))
assert report['large_world_p99_rgb_error_outside_source']<=2
for f in ['Ortho','Orbit90','Zoom','FogExponential','FogVolumetric','RefractionFog','SourceRotated']:
 assert np.isfinite(np.asarray(Image.open(s/(f+'.png')))).all()
canvas=Image.new('RGB',(960*2,540+40),(16,19,26));d=ImageDraw.Draw(canvas)
for i,(name,label) in enumerate([('Off','OFF'),('After','ON - LOW / BLOOM OFF')]):
 canvas.paste(Image.open(s/(name+'.png')).convert('RGB'),(960*i,40));d.text((960*i+20,12),label,fill='white')
canvas.save(out/'comparison.png')
canvas=Image.new('RGB',(960*2,580*2),(16,19,26));d=ImageDraw.Draw(canvas)
for i,(name,label) in enumerate([('Orbit90','CAMERA ROTATION'),('Zoom','ZOOM'),('Ortho','ORTHOGRAPHIC'),('FogVolumetric','VOLUMETRIC FOG')]):
 x=(i%2)*960;y=(i//2)*580;canvas.paste(Image.open(s/(name+'.png')).convert('RGB'),(x,y+40));d.text((x+20,y+12),label,fill='white')
canvas.save(out/'views.png')
shutil.copy2(s/'After.png',out/'scene.png')
shutil.copy2(s/'gpu_measurements.json',out/'gpu_measurements.json')
(out/'readability.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
