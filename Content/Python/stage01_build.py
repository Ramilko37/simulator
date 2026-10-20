"""One-off native editor migration. Requires -Stage01Apply and a verified project backup.

Builds a working map only. Canonical promotion follows the independent PIE checks.
"""
import json
from pathlib import Path
import unreal

assert "-Stage01Apply" in unreal.SystemLibrary.get_command_line()
PROJECT = Path(unreal.Paths.project_dir()).resolve()
SOURCE = PROJECT.parents[1] / "AssetsSource/Terrain/Stage01"
DATA = json.loads((SOURCE/"layout.json").read_text())
CANONICAL = "/Game/TrainingField/Maps/TrainingField"
WORK = "/Game/TrainingField/Maps/TrainingField_Stage01_Work03"
CHECKPOINT = "/Game/TrainingField/Tests/E0_BeforeStage01"
A = unreal.EditorAssetLibrary
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MAT = unreal.MaterialEditingLibrary


def expression(material, kind, **properties):
    node=MAT.create_material_expression(material,kind)
    for key,value in properties.items(): node.set_editor_property(key,value)
    return node


def material(name, color=None):
    path="/Game/TrainingField/Terrain/"+name
    if A.does_asset_exist(path): return unreal.load_asset(path)
    mat=TOOLS.create_asset(name,"/Game/TrainingField/Terrain",unreal.Material,unreal.MaterialFactoryNew())
    roughness=expression(mat,unreal.MaterialExpressionConstant,r=.9)
    MAT.connect_material_property(roughness,"",unreal.MaterialProperty.MP_ROUGHNESS)
    if color:
        rgb=expression(mat,unreal.MaterialExpressionVectorParameter,parameter_name="Color",default_value=unreal.LinearColor(*color,1))
    else:
        rgb=None
        for name,color in [("Grass",(.21,.29,.13)),("Dirt",(.31,.22,.13)),
                           ("Gravel",(.4,.4,.35)),("Forest",(.10,.15,.07))]:
            sample=expression(mat,unreal.MaterialExpressionLandscapeLayerSample,parameter_name=name,preview_weight=1.0 if name=="Grass" else 0.0)
            value=expression(mat,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*color,1))
            multiply=expression(mat,unreal.MaterialExpressionMultiply)
            MAT.connect_material_expressions(sample,"",multiply,"A")
            MAT.connect_material_expressions(value,"",multiply,"B")
            if rgb:
                add=expression(mat,unreal.MaterialExpressionAdd)
                MAT.connect_material_expressions(rgb,"",add,"A")
                MAT.connect_material_expressions(multiply,"",add,"B")
                rgb=add
            else: rgb=multiply
    MAT.connect_material_property(rgb,"",unreal.MaterialProperty.MP_BASE_COLOR)
    MAT.layout_material_expressions(mat)
    MAT.recompile_material(mat)
    return mat


def place(cls,label,location,folder,yaw=0):
    actor=ACTORS.spawn_actor_from_class(cls,unreal.Vector(*location),unreal.Rotator(pitch=0,yaw=yaw,roll=0))
    assert actor,label
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    return actor


def volume(label,center,half,yaw,folder,tag):
    actor=place(unreal.TriggerBox,label,center,folder,yaw)
    box=actor.get_component_by_class(unreal.BoxComponent)
    box.set_box_extent(unreal.Vector(*half))
    box.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    actor.set_actor_hidden_in_game(True)
    actor.set_editor_property("is_editor_only_actor",True)
    actor.set_editor_property("tags",[tag])
    return actor


assert LEVELS.load_level(CANONICAL)
if not A.does_asset_exist(CHECKPOINT):
    assert unreal.EditorLoadingAndSavingUtils.save_map(EDITOR.get_editor_world(),CHECKPOINT)
assert LEVELS.load_level(CHECKPOINT)
assert sum(isinstance(a,unreal.StaticMeshActor) for a in ACTORS.get_all_level_actors())==7, "E0 recovery map changed"
if not A.does_asset_exist(WORK):
    assert LEVELS.load_level(CANONICAL)
    assert unreal.EditorLoadingAndSavingUtils.save_map(EDITOR.get_editor_world(),WORK)
assert LEVELS.load_level(WORK)
for actor in ACTORS.get_all_level_actors():
    if isinstance(actor,(unreal.StaticMeshActor,unreal.PlayerStart,unreal.DroneTrainerGate,unreal.DroneTrainerLevelLayout,unreal.TriggerBox)):
        assert ACTORS.destroy_actor(actor)
    elif isinstance(actor,(unreal.DirectionalLight,unreal.SkyLight,unreal.SkyAtmosphere,unreal.ExponentialHeightFog)):
        actor.set_folder_path("Lighting")

ground=material("M_Stage01_Ground")
obstacle=material("M_Stage01_Obstacle",(.22,.28,.17))
target=material("M_Stage01_Target",(.95,.25,.035))
existing=[a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.Landscape)]
assert len(existing)<=1
land=existing[0] if existing else unreal.Stage01TerrainBuilder.import_terrain(str(SOURCE),ground)
assert land

bg=json.loads((SOURCE/"background.json").read_text())
mesh=unreal.Stage01TerrainBuilder.build_geometry("SM_BackgroundTerrain",[unreal.Vector(*p) for p in bg['vertices_cm']],bg['triangles'],obstacle)
background=place(unreal.StaticMeshActor,"T03 - Background terrain",(0,0,0),"Background")
background.static_mesh_component.set_static_mesh(mesh)
background.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

# Shared pad, ground-top pivot: upper face Z=0, thickness 2 cm below ground.
vertices=[(-400,-400,-2),(400,-400,-2),(400,400,-2),(-400,400,-2),
          (-400,-400,0),(400,-400,0),(400,400,0),(-400,400,0)]
indices=[0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7]
pad_mesh=unreal.Stage01TerrainBuilder.build_geometry("SM_TrainingPad",[unreal.Vector(*p) for p in vertices],indices,target)
pads=[]
for label,node in [("StartPad",DATA['nodes_m'][0]),("LandingPad",DATA['nodes_m'][-1])]:
    pad=place(unreal.StaticMeshActor,label,[v*100 for v in node],"Gameplay")
    pad.static_mesh_component.set_static_mesh(pad_mesh)
    pad.static_mesh_component.set_collision_profile_name("TrainingGround")
    # Explicit simple box is entirely below the top surface.
    pads.append(pad)

start=place(unreal.PlayerStart,"Start - +X",(-12000,-10000,50),"Gameplay")
start.get_component_by_class(unreal.CapsuleComponent).set_capsule_size(40,40)
gates=[]
for index,(node,yaw) in enumerate(zip(DATA['nodes_m'][1:-1],DATA['gate_yaws']),1):
    gate=place(unreal.DroneTrainerGate,"G"+str(index),[v*100 for v in node],"Route",yaw)
    gate.set_editor_property("gate_index",index)
    gate.set_editor_property("tags",["G"+str(index)])
    gate.frame.set_material(1,target)
    gates.append(gate)
layout=place(unreal.DroneTrainerLevelLayout,"Training layout",(0,0,0),"Gameplay")
layout.set_editor_property("start",start)
layout.set_editor_property("landing_pad",pads[-1])
layout.set_editor_property("gates",gates)

import math
for i,(a,b) in enumerate(zip(DATA['nodes_m'],DATA['nodes_m'][1:]),1):
    dx,dy=b[0]-a[0],b[1]-a[1]
    volume("Clear corridor %02d"%i,[(a[0]+b[0])*50,(a[1]+b[1])*50,(a[2]+b[2])*50+500],
           [math.hypot(dx,dy)*50,800,500+abs(b[2]-a[2])*50],math.degrees(math.atan2(dy,dx)),"Route/Exclusions","NoDecor")
for label,center,half in [("Start clearing",(-12000,-10000,500),(2500,2500,500)),
                          ("Landing clearing",(-7000,10000,550),(1500,1500,500)),
                          ("Technical area",(-15900,-10250,300),(1100,2250,300))]:
    volume(label,center,half,0,"Gameplay/Exclusions","NoDecor")
for label,center,half in [("North belt",(0,16750,1000),(17000,1250,1000)),
                          ("East belt",(16500,0,1000),(1500,14000,1000)),
                          ("Southwest group",(-16500,0,1000),(2000,6500,1000))]:
    volume(label,center,half,0,"Vegetation/Zones","VegetationZone")
volume("Flight bounds 400m",(0,0,2500),(20000,20000,2500),0,"Gameplay/Bounds","FlightBounds")
volume("Warning bounds 360m",(0,0,2000),(18000,18000,2000),0,"Gameplay/Bounds","WarningBounds")
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
assert LEVELS.save_current_level()
assert LEVELS.load_level(WORK)
gates=sorted((a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.DroneTrainerGate)),key=lambda a:a.gate_index)
layout=next(a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.DroneTrainerLevelLayout))
out=Path(unreal.Paths.project_saved_dir())/"Stage01-build.json"
out.write_text(json.dumps(dict(map=WORK,checkpoint=CHECKPOINT,gates=[a.get_path_name() for a in gates],
                               layout=layout.get_path_name(),source=str(SOURCE)),indent=2))
print("STAGE01_BUILD_PASS",out)
