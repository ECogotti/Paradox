"""Interleaved 4K GPU regions; run in an offscreen editor with GPU tracing enabled."""
import json,runpy,traceback
from pathlib import Path
import unreal
s=runpy.run_path(str(Path(__file__).with_name('setup_accretion_scene.py')))
e,b,world=s['extension'],s['source'],s['world']
unreal.RenderingLibrary.resize_render_target2d(s['rt'],3840,2160)
unreal.SystemLibrary.execute_console_command(world,'r.VSync 0')
unreal.SystemLibrary.execute_console_command(world,'t.MaxFPS 120')
cases=['Off','Low','NoGas','NoFilaments','NoSparks','Medium','High','OffClose','LowClose','MediumClose','HighClose','RefOffBase','RefOn','BackgroundCapture']
sequence=[]
for round_index in range(4):
    for name in (cases if round_index%2==0 else cases[::-1]):sequence.append((f'Accretion_{name}_R{round_index}',name))
state={'warm':0,'case':-1,'frame':0,'busy':False,'done':0}
def select(name):
    s['view'](unreal.Vector(1200,1400,1400),unreal.Vector(0,0,0)) if name.endswith('Close') else s['view']()
    e.quality=unreal.ParadoxAccretionQuality.HIGH if name.startswith('High') else unreal.ParadoxAccretionQuality.MEDIUM if name.startswith('Medium') else unreal.ParadoxAccretionQuality.LOW
    e.enable_gas=name!='NoGas';e.enable_filaments=name!='NoFilaments';e.enable_sparks=name!='NoSparks'
    e.enabled=name not in ('Off','OffClose','RefOffBase')
    b.set_refraction_strength(.15 if name in ('RefOn','BackgroundCapture') else 0)
    b.set_actor_tick_enabled(False)
    e.refresh_extension()
    if name in ('RefOn','BackgroundCapture'):
        cap=b.get_editor_property('background_capture')
        for key,v in {'capture_every_frame':False,'capture_on_movement':False,'main_view_camera':False,'main_view_family':False,'main_view_resolution':False,'inherit_main_view_camera_post_process_settings':False,'projection_type':unreal.CameraProjectionMode.PERSPECTIVE,'fov_angle':60.}.items():cap.set_editor_property(key,v)
        cap.set_world_location(s['capture_actor'].get_actor_location(),False,True)
        cap.set_world_rotation(s['capture_actor'].get_actor_rotation(),False,True)
        rt=b.get_editor_property('background_rt');unreal.RenderingLibrary.resize_render_target2d(rt,3840,2160)
        cap.capture_every_frame=False;cap.capture_on_movement=False
        cap.capture_scene();unreal.RenderingLibrary.read_render_target_raw_pixel(world,rt,1920,1080,False)
def begin_case():
    state['case']+=1;state['frame']=0
    if state['case']==len(sequence):state['done']=1;return
    select(sequence[state['case']][1])
def tick(delta):
    if state['busy']:return
    state['busy']=True
    try:
        if state['done']:
            state['done']+=1
            if state['done']==12:unreal.SystemLibrary.execute_console_command(world,'Trace.Stop')
            if state['done']>=18:
                (s['out']/'benchmark_regions.json').write_text(json.dumps([row[0] for row in sequence]))
                unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
            return
        if state['warm']<120:
            s['capture'].capture_scene();unreal.RenderingLibrary.read_render_target_pixel(world,s['rt'],1920,1080)
            state['warm']+=1;return
        if state['case']<0:begin_case()
        label,name=sequence[state['case']]
        if state['frame']==8:unreal.SystemLibrary.execute_console_command(world,'Trace.RegionBegin '+label)
        if name=='BackgroundCapture':
            cap=b.get_editor_property('background_capture');cap.capture_every_frame=False;cap.capture_on_movement=False;cap.capture_scene()
            unreal.RenderingLibrary.read_render_target_raw_pixel(world,b.get_editor_property('background_rt'),1920,1080,False)
        else:
            s['capture'].capture_scene();unreal.RenderingLibrary.read_render_target_pixel(world,s['rt'],1920,1080)
        state['frame']+=1
        if state['frame']==40:
            unreal.SystemLibrary.execute_console_command(world,'Trace.RegionEnd '+label);begin_case()
    except Exception:
        unreal.log_error(traceback.format_exc());unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
    finally:state['busy']=False
handle=unreal.register_slate_post_tick_callback(tick)
unreal.log('ACCRETION_BENCHMARK_REGISTERED')
