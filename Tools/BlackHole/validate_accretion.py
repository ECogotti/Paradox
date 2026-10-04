"""Behavior/readability regression without saving existing assets or levels."""
import json, runpy, math, hashlib
from pathlib import Path
import unreal
project=Path(__file__).resolve().parents[2]
original={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (project/'Content/Vfx/BlackHole').rglob('*.uasset')}
s=runpy.run_path(str(Path(__file__).with_name('setup_accretion_scene.py')))
e,b=s['extension'],s['source'];components=e.get_particle_components()
checks=[]
def check(name,ok,**data):
    checks.append(dict(name=name,passed=bool(ok),**data));unreal.log('ACCRETION_CHECK '+json.dumps(checks[-1]));assert ok,checks[-1]
unreal.log('ACCRETION_DIAGNOSTIC '+str((e.get_editor_property('active'),e.get_editor_property('state_message'),[(c.get_name(),c.is_active()) for c in components])))
check('Defaults create six valid active components',e.get_editor_property('active') and len(components)==6 and all(c.is_active() for c in components))
check('Refraction off independent of capture',b.get_editor_property('background_ready')==0 and not b.get_editor_property('background_capture').get_editor_property('capture_every_frame'))
for quality,count in [(unreal.ParadoxAccretionQuality.LOW,272),(unreal.ParadoxAccretionQuality.MEDIUM,448),(unreal.ParadoxAccretionQuality.HIGH,712)]:
    e.quality=quality;check('Quality '+str(quality),e.get_editor_property('active_particle_count')==count)
e.quality=unreal.ParadoxAccretionQuality.LOW
for kind,flag in enumerate(('enable_gas','enable_filaments','enable_sparks')):
    setattr(e,flag,False);check('Group actually stops '+str(kind),all(not components[i].is_active() and not components[i].is_component_tick_enabled() for i in (kind,kind+3)))
    setattr(e,flag,True)
e.enabled=False;check('Disabled stops all simulation',not e.get_editor_property('active') and all(not c.is_active() and not c.is_component_tick_enabled() for c in components))
e.enabled=True
value=unreal.ParadoxAccretionTuning();value.brightness=float('nan')
check('NaN setter preserves valid state',not e.set_tuning(value) and math.isfinite(e.get_editor_property('tuning').brightness) and e.get_editor_property('active'))
value=unreal.ParadoxAccretionTuning();value.gas_opacity=3
check('Out of range setter rejected',not e.set_tuning(value) and e.get_editor_property('tuning').gas_opacity<.3)
b.enable_accretion_disk=False;e.refresh_extension();check('Source disk disables extension',not e.get_editor_property('active'))
b.enable_accretion_disk=True;e.refresh_extension()
unreal.SystemLibrary.execute_console_command(s['world'],'Paradox.Accretion.InspectScene')
check('Actual populations and material bindings match after switches','materials_valid=1 exclusions_valid=1 capture_ready=0 particles=272 expected=272' in (s['out']/'scene_inspection.txt').read_text())
# Render on separate editor frames so Niagara dynamic data and capture view states update.
queue=[('Off',2,None),('After',2,None)] + [('Frame_%02d'%i,2+i/12.,None) for i in range(48)]
queue += [('Orbit90',2,('view',unreal.Vector(-6200,5200,5200),unreal.Vector(-700,0,0),False)),('Pan',2,('view',unreal.Vector(6500,6600,5200),unreal.Vector(600,400,0),False)),('Zoom',2,('view',unreal.Vector(2800,3500,2800),unreal.Vector(0,0,0),False)),('Ortho',2,('view',unreal.Vector(5200,6200,5200),unreal.Vector(-700,0,0),True))]
fog_actor=s['actors'].spawn_actor_from_class(unreal.ExponentialHeightFog,unreal.Vector(0,0,10000),transient=True)
fog=fog_actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
for k,v in {'fog_density':.01,'fog_height_falloff':.001,'fog_inscattering_luminance':unreal.LinearColor(.08,.12,.18,1),'volumetric_fog_emissive':unreal.LinearColor(.08,.12,.18,1),'volumetric_fog_distance':100000}.items():fog.set_editor_property(k,v)
fog_actor.set_actor_hidden_in_game(True)
fog.set_visibility(False)
def fog_case(volumetric):
    s['view']();fog_actor.set_actor_hidden_in_game(False);fog.set_visibility(True);fog.set_editor_property('enable_volumetric_fog',volumetric)
def refraction_case():
    b.set_refraction_strength(.15);b.set_actor_tick_enabled(False);e.refresh_extension()
    check('Native refraction capture ready',b.get_editor_property('capture_state')==unreal.ParadoxBlackHoleCaptureState.READY)
    cap=b.get_editor_property('background_capture')
    for k,v in {'capture_every_frame':False,'capture_on_movement':False,'main_view_camera':False,'main_view_family':False,'main_view_resolution':False,'inherit_main_view_camera_post_process_settings':False,'fov_angle':60.}.items():cap.set_editor_property(k,v)
    cap.set_world_location(s['capture_actor'].get_actor_location(),False,True);cap.set_world_rotation(s['capture_actor'].get_actor_rotation(),False,True)
    unreal.RenderingLibrary.resize_render_target2d(b.get_editor_property('background_rt'),960,540)
    cap.capture_every_frame=False;cap.capture_on_movement=False
    unreal.SystemLibrary.execute_console_command(s['world'],'Paradox.Accretion.InspectScene')
    inspection=(s['out']/'scene_inspection.txt').read_text()
    check('Correct MID and capture exclusions', 'materials_valid=1 exclusions_valid=1 capture_ready=1' in inspection)
def refraction_off():
    b.set_refraction_strength(0);e.refresh_extension();fog_actor.set_actor_hidden_in_game(True);fog.set_visibility(False)
    unreal.SystemLibrary.execute_console_command(s['world'],'Paradox.Accretion.InspectScene')
    check('Capture exclusions removed when off','materials_valid=1 exclusions_valid=1 capture_ready=0' in (s['out']/'scene_inspection.txt').read_text())
def large_world():
    offset=unreal.Vector(50000000,-40000000,30000000)
    for a in [b,e,s['floor']]+s['targets']:a.set_actor_location(a.get_actor_location()+offset,False,True)
    s['view'](unreal.Vector(5200,6200,5200)+offset,unreal.Vector(-700,0,0)+offset)
    e.refresh_extension();check('Large-world route remains active',e.get_editor_property('active'))
def source_rotation():
    b.get_editor_property('sphere').set_world_rotation(unreal.Rotator(20,45,15),False,True);e.refresh_extension()
    check('Rotated source remains active',e.get_editor_property('active'))
queue += [('FogExponential',2,('call',lambda:fog_case(False))),('FogVolumetric',2,('call',lambda:fog_case(True))),('RefractionFog',2,('call',refraction_case)),('CaptureOff',2,('call',refraction_off)),('LargeWorld',2,('call',large_world)),('SourceRotated',2,('call',source_rotation))]
state={'index':-1,'frames':0,'busy':False}
def next_case():
    state['index']+=1;state['frames']=0
    if state['index']>=len(queue):return
    label,time,change=queue[state['index']]
    e.enabled=label!='Off'
    if change:
        if change[0]=='view':s['view'](*change[1:])
        else:change[1]()
    v=e.get_editor_property('tuning');v.animation_time_override=time;e.set_tuning(v)
def tick(delta):
    if state['busy']:return
    state['busy']=True
    try:
        if state['index']<0:next_case()
        if state['index']>=len(queue):
            e.source_black_hole=None;check('Missing source stops with useful status',not e.get_editor_property('active') and 'assign' in e.get_editor_property('state_message').lower())
            e.source_black_hole=b;check('Source restored',e.get_editor_property('active'))
            e.enabled=False;s['actors'].destroy_actor(e)
            check('No existing saved assets changed',all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in original.items()))
            (s['out']/'validation.json').write_text(json.dumps(checks,indent=2))
            unreal.log('ACCRETION_VALIDATION_OK');unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor();return
        state['frames']+=1
        if queue[state['index']][0]=='RefractionFog':
            b.get_editor_property('background_capture').capture_scene()
            unreal.RenderingLibrary.read_render_target_raw_pixel(s['world'],b.get_editor_property('background_rt'),480,270,False)
        if state['frames']>5:
            s['render'](queue[state['index']][0]);next_case()
    except Exception:
        import traceback
        unreal.log_error(traceback.format_exc());unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
    finally:state['busy']=False
handle=unreal.register_slate_post_tick_callback(tick)
unreal.log('ACCRETION_VALIDATION_RENDER_REGISTERED')
