"""Unreal Python: creates only /Game/Vfx/BlackHole/Accretion assets."""
import json,hashlib
from pathlib import Path
import unreal
project=Path(__file__).resolve().parents[2]
out=project/'Saved/CodexBlackHoleValidation'
out.mkdir(parents=True,exist_ok=True)
root='/Game/Vfx/BlackHole/Accretion'
lib=unreal.MaterialEditingLibrary
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (project/'Content/Vfx/BlackHole').rglob('*.uasset') if 'Accretion' not in p.parts}
tools=unreal.AssetToolsHelpers.get_asset_tools()
task=unreal.AssetImportTask()
task.filename=str(project/'SourceArt/BlackHole/T_BH_AccretionGas.png')
task.destination_path=root
task.automated=True
task.replace_existing=True
task.save=False
tools.import_asset_tasks([task])
gas=unreal.load_asset(root+'/T_BH_AccretionGas')
for name,value in {'srgb':False,'compression_settings':unreal.TextureCompressionSettings.TC_MASKS,'address_x':unreal.TextureAddress.TA_CLAMP,'address_y':unreal.TextureAddress.TA_CLAMP,'filter':unreal.TextureFilter.TF_TRILINEAR,'mip_gen_settings':unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE}.items(): gas.set_editor_property(name,value)
unreal.EditorAssetLibrary.save_loaded_asset(gas)
structure=unreal.load_asset('/Game/Vfx/BlackHole/T_BH_DiskStructure')
scalar_defaults={'SourceRadius':150,'RotationSpeed':.35,'ForegroundIntensity':.2,'MidgroundIntensity':1,'GasOpacity':.16,'Brightness':1.2,'LayerTransition':450,'Lifetime':18,'Turbulence':.22,'ParticleScale':1,'TimeOverride':-1,'Layer':0}
vectors={'RouteStart':(-300000,0,0),'RouteA':(-200000,10000,10000),'RouteB':(-60000,-10000,5000),'PlaneOrigin':(0,0,0),'PlaneUp':(0,0,1),'SourceCenter':(-300000,0,0),'SourceAxis':(0,0,1),'LocalExtent':(6000,6000,1800),'InnerColor':(.473936,.866877,1),'OuterColor':(.40625,.505869,1)}
report={}
for kind_index,kind in enumerate(('Gas','Filaments','Sparks')):
    path=root+'/MM_Accretion_'+kind
    mat=unreal.load_asset(path)
    if mat:
        for n in unreal.ObjectIterator(unreal.MaterialExpressionScalarParameter):
            if n.get_outer()==mat and str(n.get_editor_property('parameter_name')) in scalar_defaults:
                n.set_editor_property('default_value',scalar_defaults[str(n.get_editor_property('parameter_name'))])
        expressions=[n for n in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if n.get_outer()==mat]
        assert len(expressions)==2,(path,expressions)
        for n in expressions:
            filename='AccretionExtensionPosition.hlsl' if n.get_editor_property('output_type')==unreal.CustomMaterialOutputType.CMOT_FLOAT3 else 'AccretionExtensionShade.hlsl'
            n.set_editor_property('code',(project/'Shaders'/filename).read_text())
    else:
        mat=tools.create_asset(path.rsplit('/',1)[1],root,unreal.Material,unreal.MaterialFactoryNew())
        for name,value in {'blend_mode':unreal.BlendMode.BLEND_TRANSLUCENT,'shading_model':unreal.MaterialShadingModel.MSM_UNLIT,'two_sided':True,'used_with_niagara_mesh_particles':True,'use_translucency_vertex_fog':True,'compute_fog_per_pixel':True,'translucency_pass':unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF,'disable_depth_test':False}.items():mat.set_editor_property(name,value)
        inputs={}
        def node(cls,**props):
            n=lib.create_material_expression(mat,getattr(unreal,cls))
            for k,v in props.items():n.set_editor_property(k,v)
            return n
        def connect(a,b,p='',output=''):
            assert lib.connect_material_expressions(a,output,b,p),(a,b,p)
        for name,value in scalar_defaults.items(): inputs[name]=node('MaterialExpressionScalarParameter',parameter_name=unreal.Name(name),default_value=value)
        for name,value in vectors.items():inputs[name]=node('MaterialExpressionVectorParameter',parameter_name=unreal.Name(name),default_value=unreal.LinearColor(*value,0))
        inputs['Kind']=node('MaterialExpressionConstant',r=float(kind_index))
        inputs['Time']=node('MaterialExpressionTime')
        inputs['Seed']=node('MaterialExpressionParticleRandom')
        inputs['UV']=node('MaterialExpressionTextureCoordinate')
        origin=node('MaterialExpressionActorPositionWS')
        for name,pos in [('WorldRel',node('MaterialExpressionWorldPosition')),('OriginalWorldRel',node('MaterialExpressionWorldPosition',world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)),('CameraRel',node('MaterialExpressionCameraPositionWS'))]:
            sub=node('MaterialExpressionSubtract');connect(pos,sub,'A');connect(origin,sub,'B');inputs[name]=sub
        for name,axis in [('CameraRight',(1,0,0)),('CameraUp',(0,1,0)),('CameraForward',(0,0,1))]:
            c=node('MaterialExpressionConstant3Vector',constant=unreal.LinearColor(*axis,0))
            t=node('MaterialExpressionTransform',transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_CAMERA,transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
            connect(c,t);inputs[name]=t
        inputs['GasTex']=node('MaterialExpressionTextureObjectParameter',parameter_name=unreal.Name('GasTex'),texture=gas,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        inputs['DiskStructure']=node('MaterialExpressionTextureObjectParameter',parameter_name=unreal.Name('DiskStructure'),texture=structure,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        def custom(filename,output_type,names):
            c=node('MaterialExpressionCustom',code=(project/'Shaders'/filename).read_text(),output_type=output_type)
            items=[]
            for name in names:
                item=unreal.CustomInput();item.set_editor_property('input_name',unreal.Name(name));items.append(item)
            c.set_editor_property('inputs',items)
            for name in names:connect(inputs[name],c,name)
            return c
        common=list(scalar_defaults)+list(vectors)+['Kind','Time','Seed','UV','CameraForward']
        position=custom('AccretionExtensionPosition.hlsl',unreal.CustomMaterialOutputType.CMOT_FLOAT3,common+['OriginalWorldRel','CameraRight','CameraUp'])
        shade=custom('AccretionExtensionShade.hlsl',unreal.CustomMaterialOutputType.CMOT_FLOAT4,common+['WorldRel','CameraRel','GasTex','DiskStructure'])
        assert lib.connect_material_property(position,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        rgb=node('MaterialExpressionComponentMask',r=True,g=True,b=True,a=False);connect(shade,rgb)
        alpha=node('MaterialExpressionComponentMask',r=False,g=False,b=False,a=True);connect(shade,alpha)
        fade=node('MaterialExpressionDepthFade',fade_distance_default=80);connect(alpha,fade,'Opacity')
        assert lib.connect_material_property(rgb,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        assert lib.connect_material_property(fade,'',unreal.MaterialProperty.MP_OPACITY)
    errors=list(lib.recompile_material(mat))
    assert not errors,errors
    unreal.AutomationUtilsBlueprintLibrary.finish_all_asset_compilation()
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    mi_path=root+'/MI_Accretion_'+kind
    mi=unreal.load_asset(mi_path) or tools.create_asset(mi_path.rsplit('/',1)[1],root,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    lib.set_material_instance_parent(mi,mat)
    lib.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    report[kind]={'compiled':True,'material':mat.get_path_name()}
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'Paradox.Accretion.BuildSystems')
assert (out/'accretion_system_build.txt').read_text()=='Success: 3 systems compiled and saved', 'Native Niagara generation failed; inspect LogParadox errors.'
for kind in report:
    system=unreal.load_asset(root+'/NS_Accretion_'+kind)
    assert system,kind
    system.set_editor_property('determinism',True)
    system.set_editor_property('random_seed',240104)
    unreal.AutomationUtilsBlueprintLibrary.finish_all_asset_compilation()
    unreal.EditorAssetLibrary.save_loaded_asset(system)
report['previous_assets_unchanged']=all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in before.items())
assert report['previous_assets_unchanged']
(out/'accretion_asset_creation.json').write_text(json.dumps(report,indent=2))
unreal.log('ACCRETION_ASSETS_CREATED '+json.dumps(report))
