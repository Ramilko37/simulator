"""Stage 04 editor operations, dispatched through Unreal MCP's editor console.

Call import_layer once per layer, then build and verify. Backup E3 is required.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve().parents[1]
SOURCE = ROOT / 'AssetsSource/Materials/Stage04'
EVIDENCE = ROOT / 'docs/prototype-level/evidence/stage04/revision-02'
TEX = '/Game/TrainingField/Textures/Stage04/'
MATPATH = '/Game/TrainingField/Materials/'
A = unreal.EditorAssetLibrary
M = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def invariants():
    rows = []
    for actor in ACTORS.get_all_level_actors():
        loc, rot, scale = actor.get_actor_location(), actor.get_actor_rotation(), actor.get_actor_scale3d()
        row = dict(path=actor.get_path_name(), label=actor.get_actor_label(),
                   location=[loc.x, loc.y, loc.z], rotation=[rot.pitch, rot.yaw, rot.roll],
                   scale=[scale.x, scale.y, scale.z])
        row['primitives'] = [dict(path=c.get_path_name(), collision=str(c.get_collision_enabled()),
                                  profile=str(c.get_collision_profile_name()))
                             for c in actor.get_components_by_class(unreal.PrimitiveComponent)]
        if isinstance(actor, unreal.StaticMeshActor):
            row['mesh'] = actor.static_mesh_component.static_mesh.get_path_name()
        rows.append(row)
    return sorted(rows, key=lambda r: r['path'])


def import_layer(layer):
    names = [f'T_{layer}_{s}' for s in ['BC', 'N', 'ORM']] if layer != 'Macro' else ['T_MacroVariation']
    for name in names:
        task = unreal.AssetImportTask()
        task.filename = str(SOURCE / 'runtime' / (name + '.png'))
        task.destination_path = TEX.rstrip('/')
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        task.factory = unreal.TextureFactory()
        TOOLS.import_asset_tasks([task])
        tex = unreal.load_asset(TEX + name)
        assert isinstance(tex, unreal.Texture2D), (name, task.imported_object_paths)
        normal = name.endswith('_N')
        color = name.endswith('_BC')
        settings = dict(srgb=color, flip_green_channel=False, lod_bias=0,
                        max_texture_size=512 if layer == 'Macro' else 2048,
                        never_stream=False, virtual_texture_streaming=False,
                        mip_gen_settings=unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP,
                        compression_settings=unreal.TextureCompressionSettings.TC_NORMALMAP if normal else
                        unreal.TextureCompressionSettings.TC_DEFAULT if color else unreal.TextureCompressionSettings.TC_MASKS,
                        lod_group=unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP if normal else unreal.TextureGroup.TEXTUREGROUP_WORLD)
        for key, value in settings.items(): tex.set_editor_property(key, value)
        assert A.save_loaded_asset(tex)
    print('STAGE04_IMPORT_PASS', layer)


def expression(mat, cls, **properties):
    node = M.create_material_expression(mat, cls)
    assert node
    for key, value in properties.items(): node.set_editor_property(key, value)
    return node


def connect(source, target, pin, output=''):
    assert M.connect_material_expressions(source, output, target, pin), (source, target, pin, output)


def parameter(mat, name, value, group):
    return expression(mat, unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value, group=group)


def new_material(name):
    mat = unreal.load_asset(MATPATH + name) if A.does_asset_exist(MATPATH + name) else TOOLS.create_asset(name, MATPATH.rstrip('/'), unreal.Material, unreal.MaterialFactoryNew())
    assert mat
    M.delete_all_material_expressions(mat)
    return mat


def build():
    assert (ROOT / 'Backups/PreviousStable/source.tar.gz').exists()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Save user work before material assignment'
    before = invariants()
    (EVIDENCE / 'invariants-before.json').write_text(json.dumps(before, indent=2))
    actors = ACTORS.get_all_level_actors()
    land = next(a for a in actors if isinstance(a, unreal.Landscape))
    assert '/Game/TrainingField/Maps/TrainingField.' in land.get_path_name()
    passports = json.loads((SOURCE / 'passports.json').read_text())
    mat = new_material('M_Landscape_TrainingField')
    world = expression(mat, unreal.MaterialExpressionWorldPosition)
    xy = expression(mat, unreal.MaterialExpressionComponentMask, r=True, g=True)
    connect(world, xy, '', 'XYZ')
    sums = {}
    for p in passports:
        layer = p['layer']
        tile = parameter(mat, layer + 'TileCm', p['tile_m'] * 100, 'Detail scale (cm)')
        uv = expression(mat, unreal.MaterialExpressionDivide)
        connect(xy, uv, 'A'); connect(tile, uv, 'B')
        samples = {}
        for suffix in ['BC', 'N', 'ORM']:
            sample = expression(mat, unreal.MaterialExpressionTextureSampleParameter2D,
                                parameter_name=layer + suffix, group=layer,
                                texture=unreal.load_asset(TEX + 'T_' + layer + '_' + suffix),
                                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if suffix == 'N' else
                                unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if suffix == 'BC' else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                                sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
            connect(uv, sample, 'UVs'); samples[suffix] = sample
        # LayerWeight prunes unused texture branches per Landscape component.
        for channel, suffix, output in [('color', 'BC', 'RGB'), ('normal', 'N', 'RGB'), ('ao', 'ORM', 'R'), ('roughness', 'ORM', 'G')]:
            weight = expression(mat, unreal.MaterialExpressionLandscapeLayerWeight,
                                parameter_name=layer, preview_weight=1.0 if layer == 'Grass' else 0.0,
                                const_base=unreal.Vector(0, 0, 0))
            connect(samples[suffix], weight, 'Layer', output)
            if channel in sums: connect(sums[channel], weight, 'Base')
            sums[channel] = weight
    macro_uv = expression(mat, unreal.MaterialExpressionDivide)
    connect(xy, macro_uv, 'A'); connect(parameter(mat, 'MacroTileCm', 3000, 'Macro'), macro_uv, 'B')
    macro = expression(mat, unreal.MaterialExpressionTextureSampleParameter2D,
                       parameter_name='MacroTexture', group='Macro', texture=unreal.load_asset(TEX + 'T_MacroVariation'),
                       sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                       sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    connect(macro_uv, macro, 'UVs')
    variation = expression(mat, unreal.MaterialExpressionLinearInterpolate, const_a=0.88, const_b=1.04)
    connect(macro, variation, 'Alpha', 'R')
    color = expression(mat, unreal.MaterialExpressionMultiply)
    connect(sums['color'], color, 'A'); connect(variation, color, 'B')
    normal = expression(mat, unreal.MaterialExpressionNormalize); connect(sums['normal'], normal, '')
    for node, prop in [(color, unreal.MaterialProperty.MP_BASE_COLOR), (normal, unreal.MaterialProperty.MP_NORMAL),
                       (sums['ao'], unreal.MaterialProperty.MP_AMBIENT_OCCLUSION), (sums['roughness'], unreal.MaterialProperty.MP_ROUGHNESS)]:
        assert M.connect_material_property(node, '', prop)
    zero = expression(mat, unreal.MaterialExpressionConstant, r=0.0)
    assert M.connect_material_property(zero, '', unreal.MaterialProperty.MP_METALLIC)
    M.layout_material_expressions(mat); M.recompile_material(mat)
    name = 'MI_Landscape_TrainingField'
    mi = unreal.load_asset(MATPATH + name) if A.does_asset_exist(MATPATH + name) else TOOLS.create_asset(name, MATPATH.rstrip('/'), unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    M.set_material_instance_parent(mi, mat); M.update_material_instance(mi)
    assert A.save_loaded_asset(mat) and A.save_loaded_asset(mi)
    land.set_editor_property('landscape_material', mi)
    # Keep the pad mesh, collision and scene markings; only change its surface.
    pad = new_material('M_TrainingPad')
    color = expression(pad, unreal.MaterialExpressionVectorParameter, parameter_name='PadColor',
                       default_value=unreal.LinearColor(0.26, 0.17, 0.055, 1), group='Surface')
    rough = parameter(pad, 'Roughness', 0.86, 'Surface')
    metal = expression(pad, unreal.MaterialExpressionConstant, r=0.0)
    for node, prop in [(color, unreal.MaterialProperty.MP_BASE_COLOR), (rough, unreal.MaterialProperty.MP_ROUGHNESS), (metal, unreal.MaterialProperty.MP_METALLIC)]:
        assert M.connect_material_property(node, '', prop)
    M.layout_material_expressions(pad); M.recompile_material(pad); assert A.save_loaded_asset(pad)
    for actor in actors:
        if actor.get_actor_label() in ['StartPad', 'LandingPad']:
            actor.static_mesh_component.set_material(0, pad)
    assert invariants() == before, 'Material edit changed transforms or collision'
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    print('STAGE04_BUILD_PASS')


def verify():
    assert invariants() == json.loads((EVIDENCE / 'invariants-before.json').read_text())
    land = next(a for a in ACTORS.get_all_level_actors() if isinstance(a, unreal.Landscape))
    assert land.landscape_material.get_path_name() == MATPATH + 'MI_Landscape_TrainingField.MI_Landscape_TrainingField'
    mi = land.landscape_material
    mat = unreal.load_asset(MATPATH + 'M_Landscape_TrainingField')
    # The parent without component weights correctly prunes all detail branches.
    textures = {t.get_path_name(): t for instance in unreal.ObjectIterator(unreal.MaterialInstanceConstant)
                if instance.get_path_name().startswith(land.get_path_name() + '.')
                for t in M.get_material_used_textures(instance) if t.get_path_name().startswith(TEX)}
    assert len(textures) == 13, list(textures)
    rows = []
    for tex in textures.values():
        name = tex.get_name(); normal = name.endswith('_N'); color = name.endswith('_BC')
        assert tex.srgb == color and not tex.flip_green_channel and not tex.never_stream
        assert tex.lod_bias == 0 and tex.max_texture_size == (512 if name == 'T_MacroVariation' else 2048)
        rows.append(dict(path=tex.get_path_name(), built_size=str(tex.blueprint_get_built_texture_size()),
                         srgb=tex.srgb, compression=str(tex.compression_settings), group=str(tex.lod_group),
                         max_size=tex.max_texture_size, lod_bias=tex.lod_bias, mip=str(tex.mip_gen_settings),
                         never_stream=tex.never_stream, flip_green=tex.flip_green_channel))
    for layer in ['Grass', 'Dirt', 'Gravel', 'Forest']:
        assert A.does_asset_exist('/Game/TrainingField/Terrain/LI_' + layer)
        value = M.get_material_instance_scalar_parameter_value(mi, layer + 'TileCm')
        assert 200 <= value <= 400, (layer, value)
    assert M.get_material_instance_scalar_parameter_value(mi, 'MacroTileCm') == 3000
    for actor in ACTORS.get_all_level_actors():
        if actor.get_actor_label() in ['StartPad', 'LandingPad']:
            assert actor.static_mesh_component.get_material(0).get_name() == 'M_TrainingPad'
    (EVIDENCE / 'editor-after.json').write_text(json.dumps(dict(invariants='PASS', textures=rows,
                                                              landscape=mi.get_path_name()), indent=2))
    print('STAGE04_VERIFY_PASS')
