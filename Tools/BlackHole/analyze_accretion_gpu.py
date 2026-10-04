"""Analyze alternating GPU scene-render regions exported from Unreal Insights."""
import csv,json,re,statistics
from pathlib import Path
base=Path(__file__).resolve().parents[2]/'Saved/CodexBlackHoleValidation/Accretion'
regions={n:(float(a),float(b)) for n,a,b in re.findall(r"region '(Accretion_[^']+)' \[([\d.]+) \.\. ([\d.]+)\]",(base/'export.log').read_text())}
names=json.loads((base/'benchmark_regions.json').read_text())
assert len(regions)==len(names)==56,(len(regions),len(names))
events=list(csv.DictReader((base/'events.csv').open(newline='')))
def samples(name):
 a,b=regions[name]
 values=[float(e['Duration'])*1000 for e in events if e['TimerName']=='SceneRender' and a<=float(e['StartTime']) and float(e['EndTime'])<=b]
 assert len(values)==32,(name,len(values))
 return values
labels=['Off','Low','NoGas','NoFilaments','NoSparks','Medium','High','OffClose','LowClose','MediumClose','HighClose','RefOffBase','RefOn','BackgroundCapture']
report={'resolution':[3840,2160],'gpu':'NVIDIA RTX 3080','engine':'UE 5.8.3 / D3D12 SM6','source_ray_steps':128,'rounds':4,'frames_per_round':32,'scope':'transient grid/targets fixture, fixed exposure, Bloom off, ParticleScale 1','scene_render':{}}
for label in labels:
 v=[samples('Accretion_'+label+'_R'+str(i)) for i in range(4)]
 row={'median_ms':statistics.median(x for a in v for x in a),'round_medians_ms':[statistics.median(a) for a in v]}
 report['scene_render'][label]=row
for label in labels:
 if label=='BackgroundCapture':continue
 baseline='OffClose' if label.endswith('Close') else 'Off'
 row=report['scene_render'][label]
 row['paired_delta_ms']=statistics.median(row['round_medians_ms'][i]-report['scene_render'][baseline]['round_medians_ms'][i] for i in range(4))
report['primary_low_budget_passed']=max(report['scene_render'][l]['paired_delta_ms'] for l in ('Low','LowClose'))<=1
report['refraction_background_capture_ms']=report['scene_render']['BackgroundCapture']['median_ms']
(base/'gpu_measurements.json').write_text(json.dumps(report,indent=2))
print(json.dumps({l:{k:round(v,3) for k,v in row.items() if not isinstance(v,list)} for l,row in report['scene_render'].items()},indent=2))
