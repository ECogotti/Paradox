"""Unsaved, compact gameplay readability fixture. Run inside Unreal Python."""
from pathlib import Path
import unreal, math
project=Path(__file__).resolve().parents[2]
out=project/'Saved/CodexBlackHoleValidation/Accretion'
out.mkdir(parents=True,exist_ok=True)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
lib=unreal.MaterialEditingLibrary
def material(name,code):
    m=unreal.new_object(unreal.Material,name=name)
    m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    c=lib.create_material_expression(m,unreal.MaterialExpressionCustom)
    c.set_editor_property('code',code)
    c.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    item=unreal.CustomInput();item.set_editor_property('input_name',unreal.Name('UV'))
    c.set_editor_property('inputs',[item])
    uv=lib.create_material_expression(m,unreal.MaterialExpressionTextureCoordinate)
    assert lib.connect_material_expressions(uv,'',c,'UV')
    assert lib.connect_material_property(c,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert not list(lib.recompile_material(m))
    return m
floor_mat=material('M_AccretionFixtureFloor','float2 P=UV*20; float E=step(.08,frac(P.x))*step(.08,frac(P.y)); return lerp(float3(.018,.025,.035),float3(.07,.105,.14),E);')
target_mat=material('M_AccretionFixtureTargets','float2 Q=abs(UV-.5); return lerp(float3(.9,.23,.035),float3(.08,.03,.01),step(.35,max(Q.x,Q.y)));')
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
def mesh(location,scale,mat):
    a=actors.spawn_actor_from_class(unreal.StaticMeshActor,location,transient=True)
    c=a.get_editor_property('static_mesh_component');c.set_static_mesh(cube);c.set_material(0,mat)
    a.set_actor_scale3d(scale);return a
floor=mesh(unreal.Vector(0,0,-100),unreal.Vector(45,45,1),floor_mat)
targets=[]
for x,y in [(-1500,-1500),(-600,500),(500,-900),(1400,1000)]:
    targets.append(mesh(unreal.Vector(x,y,70),unreal.Vector(2,2,1.5),target_mat))
source=actors.spawn_actor_from_class(unreal.ParadoxBlackHole,unreal.Vector(7000,0,0),transient=True)
source.set_refraction_strength(0)
source.get_editor_property('spring_arm').set_editor_property('target_arm_length',12000.)
source.get_editor_property('sphere').set_world_location(unreal.Vector(-5000,0,0),False,True)
source.get_editor_property('sphere').set_world_scale3d(unreal.Vector(40,40,40))
source.get_editor_property('directional_light').set_visibility(False)
source.set_scalar_parameter('DiskThickness',.01)
source.set_scalar_parameter('RaySteps',128)
source.set_color_parameter('InnerColor',unreal.LinearColor(.473936,.866877,1,1))
source.set_color_parameter('OuterColor',unreal.LinearColor(.40625,.505869,1,1))
extension=actors.spawn_actor_from_class(unreal.ParadoxAccretionExtension,unreal.Vector(),transient=True)
extension.source_black_hole=source
tuning=extension.get_editor_property('tuning')
tuning.local_extent=unreal.Vector(4300,4300,2200)
tuning.animation_time_override=2.
assert extension.set_tuning(tuning)
capture_actor=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(5200,6200,5200),transient=True)
capture=capture_actor.get_editor_property('capture_component2d')
for k,v in {'capture_every_frame':False,'capture_on_movement':False,'capture_source':unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,'fov_angle':60.,'main_view_family':False,'main_view_resolution':False,'main_view_camera':False,'inherit_main_view_camera_post_process_settings':False}.items():capture.set_editor_property(k,v)
pp=capture.get_editor_property('post_process_settings')
for k,v in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,'override_auto_exposure_bias':True,'auto_exposure_bias':0.,'override_bloom_intensity':True,'bloom_intensity':0.,'override_vignette_intensity':True,'vignette_intensity':0.,'override_motion_blur_amount':True,'motion_blur_amount':0.}.items():pp.set_editor_property(k,v)
capture.set_editor_property('post_process_settings',pp);capture.set_editor_property('post_process_blend_weight',1.)
flags=[]
for name in ('Bloom','MotionBlur','EyeAdaptation','LocalExposure','ScreenPercentage'):
    f=unreal.EngineShowFlagsSetting();f.set_editor_property('show_flag_name',name);f.set_editor_property('enabled',False);flags.append(f)
capture.set_editor_property('show_flag_settings',flags)
rt=unreal.RenderingLibrary.create_render_target2d(world,960,540,unreal.TextureRenderTargetFormat.RTF_RGBA8)
rt.set_editor_property('target_gamma',2.2)
capture.set_editor_property('texture_target',rt)
def view(location=unreal.Vector(5200,6200,5200),target=unreal.Vector(-700,0,0),ortho=False,width=11500.):
    capture_actor.set_actor_location(location,False,True)
    capture_actor.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,target),True)
    capture.set_editor_property('projection_type',unreal.CameraProjectionMode.ORTHOGRAPHIC if ortho else unreal.CameraProjectionMode.PERSPECTIVE)
    capture.set_editor_property('ortho_width',width)
def warm():
    unreal.AutomationUtilsBlueprintLibrary.finish_all_asset_compilation()
def render(name=None,time=None):
    if time is not None:
        v=extension.get_editor_property('tuning');v.animation_time_override=time;extension.set_tuning(v)
    warm();capture.capture_scene()
    unreal.RenderingLibrary.read_render_target_pixel(world,rt,480,270)
    if name:unreal.RenderingLibrary.export_render_target(world,rt,str(out),name+'.png')
    return rt
view()
