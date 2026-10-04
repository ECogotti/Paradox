"""Run with Unreal Python after compiling ParadoxEditor; saves only modular assets.

102 static MICs are hard referenced by the native catalog. At runtime the actor
selects a parent; it never edits a static switch or compiles a material.
"""
import hashlib
import json
from pathlib import Path
import unreal

project = Path(__file__).resolve().parents[2]
out = project/'Saved/CodexBlackHoleValidation'
out.mkdir(parents=True,exist_ok=True)
root = '/Game/Vfx/BlackHole'
lib = unreal.MaterialEditingLibrary
switches = ('EnableDisk','EnableLensing','EnableRefraction','EnableNoise',
            'EnableStructures','EnableVolume','EnableDoppler','EnableGlow')

def normalize(mask):
    if not mask & 2: mask &= ~4
    if not mask & 1: mask &= ~(8|16|32|64)
    return mask

masks = sorted(set(normalize(m) for m in range(256)))
assert len(masks)==102
before = {str(p):hashlib.sha256(p.read_bytes()).hexdigest()
          for p in (project/'Content/Vfx/BlackHole').glob('*.uasset')
          if 'Modular' not in p.name and 'MaterialVariants' not in p.name}
master_path=root+'/MM_BlackHole_FogSafe_Modular'
source = unreal.load_asset(root+'/MM_BlackHole_FogSafe_Structured_Doppler')
source_mi = unreal.load_asset(root+'/MI_BlackHole_FogSafe_Structured_Doppler')
mat = unreal.load_asset(master_path) if unreal.EditorAssetLibrary.does_asset_exist(master_path) else unreal.EditorAssetLibrary.duplicate_asset(source.get_path_name(),master_path)
custom=next(n for n in lib.get_material_expressions(mat) if isinstance(n,unreal.MaterialExpressionCustom))
old_inputs=list(custom.get_editor_property('inputs'))
connections=dict(zip((str(i.get_editor_property('input_name')) for i in old_inputs),lib.get_inputs_for_material_expression(mat,custom)))
inputs=old_inputs[:]
for name in switches:
    if name not in connections:
        item=unreal.CustomInput()
        item.set_editor_property('input_name',unreal.Name(name))
        inputs.append(item)
custom.set_editor_property('inputs',inputs)
constants=[]
for val in (0.,1.):
    n=next((n for n in lib.get_material_expressions(mat)
            if isinstance(n,unreal.MaterialExpressionConstant) and n.get_editor_property('r')==val),None)
    n=n or lib.create_material_expression(mat,unreal.MaterialExpressionConstant)
    n.set_editor_property('r',val)
    constants.append(n)
switch_nodes={}
for index,name in enumerate(switches):
    n=next((n for n in lib.get_material_expressions(mat)
            if isinstance(n,unreal.MaterialExpressionStaticSwitchParameter)
            and str(n.get_editor_property('parameter_name'))==name),None)
    n=n or lib.create_material_expression(mat,unreal.MaterialExpressionStaticSwitchParameter,-700,index*150)
    n.set_editor_property('parameter_name',unreal.Name(name))
    n.set_editor_property('default_value',True)
    n.set_editor_property('group',unreal.Name('Compiled Features'))
    pins=lib.get_material_expression_input_names(n)
    assert len(pins)==2,pins
    assert lib.connect_material_expressions(constants[1],'',n,pins[0])
    assert lib.connect_material_expressions(constants[0],'',n,pins[1])
    assert lib.connect_material_expressions(n,'',custom,name)
    switch_nodes[name]=n
# Remove SceneDepth usage itself from non-refraction permutations, not just its Custom arithmetic.
depth=connections['SceneDepthDependency'] if isinstance(connections['SceneDepthDependency'],unreal.MaterialExpressionStaticSwitchParameter) else lib.create_material_expression(mat,unreal.MaterialExpressionStaticSwitchParameter,-350,1550)
depth.set_editor_property('parameter_name',unreal.Name('EnableRefraction'))
depth.set_editor_property('default_value',True)
pins=lib.get_material_expression_input_names(depth)
assert len(pins)==2,pins
if depth!=connections['SceneDepthDependency']:
    assert lib.connect_material_expressions(connections['SceneDepthDependency'],'',depth,pins[0])
assert lib.connect_material_expressions(constants[0],'',depth,pins[1])
assert lib.connect_material_expressions(depth,'',custom,'SceneDepthDependency')
# Parameterized textures let the native actor reuse its private RT across parent changes.
for name in ('BackgroundTex','DiskNoise'):
    fixed=connections[name]
    tex=fixed.get_editor_property('texture')
    param=next((n for n in lib.get_material_expressions(mat)
                if isinstance(n,unreal.MaterialExpressionTextureObjectParameter)
                and str(n.get_editor_property('parameter_name'))==name),None)
    param=param or lib.create_material_expression(mat,unreal.MaterialExpressionTextureObjectParameter)
    param.set_editor_property('parameter_name',unreal.Name(name))
    param.set_editor_property('texture',tex)
    param.set_editor_property('sampler_type',fixed.get_editor_property('sampler_type'))
    assert lib.connect_material_expressions(param,'',custom,name)
custom.set_editor_property('code',(project/'Shaders/BlackHoleFogSafe_Modular.hlsl').read_text())
errors=list(lib.recompile_material(mat))
unreal.AutomationUtilsBlueprintLibrary.finish_all_asset_compilation()
assert not errors,[str(e) for e in errors]
assert all(lib.get_inputs_for_material_expression(mat,custom))
assert unreal.EditorAssetLibrary.save_loaded_asset(mat)
report={'master':master_path,'inputs':len(inputs),'variants':{},'old_assets_unchanged':False}
entries=[]
for mask in masks:
    name=f'MI_BlackHole_Flags_{mask:02X}'
    path=root+'/Variants/'+name
    mi=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.EditorAssetLibrary.duplicate_asset(source_mi.get_path_name(),path)
    lib.set_material_instance_parent(mi,mat)
    lib.update_material_instance(mi)
    for index,name in enumerate(switches):
        lib.set_material_instance_static_switch_parameter_value(mi,unreal.Name(name),bool(mask&(1<<index)),unreal.MaterialParameterAssociation.GLOBAL_PARAMETER,False)
    lib.update_material_instance(mi)
    unreal.AutomationUtilsBlueprintLibrary.finish_all_asset_compilation()
    for index,name in enumerate(switches):
        assert lib.get_material_instance_static_switch_parameter_value(mi,unreal.Name(name))==bool(mask&(1<<index))
    entry=unreal.ParadoxBlackHoleMaterialVariant()
    entry.set_editor_property('feature_mask',mask)
    entry.set_editor_property('material',mi)
    entries.append(entry)
    stats=lib.get_statistics(mi)
    report['variants'][str(mask)]={'asset':path,'statistics':str(stats)}
    assert unreal.EditorAssetLibrary.save_loaded_asset(mi)
    unreal.log(f'CODEX_BH_VARIANT_COMPILED {mask:02X} '+str(stats))
catalog_path=root+'/DA_BlackHoleMaterialVariants'
catalog=unreal.load_asset(catalog_path) if unreal.EditorAssetLibrary.does_asset_exist(catalog_path) else None
if not catalog:
    factory=unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class',unreal.ParadoxBlackHoleMaterialVariants)
    catalog=unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_BlackHoleMaterialVariants',root,unreal.ParadoxBlackHoleMaterialVariants,factory)
assert catalog
catalog.set_editor_property('variants',entries)
profiles=[]
for suffix,mask in (('FogSafe',207),('FogSafe_Doppler',207),('FogSafe_NoDoppler',143),
                    ('FogSafe_Structured_Doppler',255),('FogSafe_Structured_NoDoppler',191)):
    source=unreal.load_asset(root+'/MI_BlackHole_'+suffix)
    assert source
    entry=unreal.ParadoxBlackHoleMaterialVariant()
    entry.set_editor_property('feature_mask',mask)
    entry.set_editor_property('material',source)
    profiles.append(entry)
catalog.set_editor_property('source_profiles',profiles)
assert unreal.EditorAssetLibrary.save_loaded_asset(catalog)
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in before.items())
report['old_assets_unchanged']=True
report['catalog']=catalog_path
(out/'features_material_creation.json').write_text(json.dumps(report,indent=2))
unreal.log('CODEX_BH_FEATURE_MATERIALS_CREATED '+json.dumps({'count':len(entries),'catalog':catalog_path,'old_assets_unchanged':True}))
