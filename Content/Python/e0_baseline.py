"""Run with UE PythonScript commandlet on a backed-up project; -E0Apply normalizes E0.

Without -E0Apply this only reads the canonical map and verifies saved calibration.
"""
import json
import math
from pathlib import Path

import unreal

MAP = "/Game/TrainingField/Maps/TrainingField"
DEMO = "/Game/TrainingField/Tests/E0_Demonstration"
CALIBRATION = "/Game/TrainingField/Tests/E0_Calibration"
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assets = unreal.EditorAssetLibrary


def upright(actor, yaw, ground_z):
    # Named fields avoid Python Rotator's positional roll/pitch/yaw order.
    actor.set_actor_rotation(unreal.Rotator(pitch=0, yaw=yaw, roll=0), False)
    origin, extent = actor.get_actor_bounds(False)
    location = actor.get_actor_location()
    location.z += ground_z - (origin.z - extent.z)
    actor.set_actor_location(location, False, False)


def spawn(mesh_path, label, location, scale=(1, 1, 1), yaw=0):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
    assert actor, label
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(unreal.load_asset(mesh_path))
    actor.set_actor_scale3d(unreal.Vector(*scale))
    upright(actor, yaw, 0)
    return actor


def decoration(label):
    return label.startswith(("Forest Belt", "Field Shrub", "Observation Tower",
                             "Prototype - Tree", "Prototype - Bush", "Prototype - Tower")) or label in {
        "Training Gate", "Field Cabin", "Field Barrier", "Equipment Case"}


def normalize():
    assert levels.load_level(MAP)
    assert not assets.does_asset_exist(DEMO), "Already migrated; run without -E0Apply"
    world = editor.get_editor_world()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, DEMO)
    # The test map retains every original sample, including its reproducible defects.
    assert levels.load_level(MAP)
    removed = []
    for actor in actors.get_all_level_actors():
        label = actor.get_actor_label()
        if decoration(label):
            removed.append(label)
            assert actors.destroy_actor(actor)
        elif label.startswith("Prototype - Gate"):
            # FBX is normalized to 200 cm overall height and centered at mid-height.
            # Source corrected.glb is 520 cm tall with a 400 cm opening.
            actor.set_actor_scale3d(unreal.Vector(2.6, 2.6, 2.6))
            upright(actor, 90, -10)
        elif label == "Training Field - Dirt Path":
            actor.set_actor_rotation(unreal.Rotator(pitch=0, yaw=90, roll=0), False)
            location = actor.get_actor_location()
            location.z = -9
            actor.set_actor_location(location, False, False)
    assert len(removed) == 125, len(removed)
    assert levels.save_current_level()

    # Keep the existing small ground and gameplay coordinates; E1 owns the new terrain.
    assert unreal.EditorLoadingAndSavingUtils.save_map(editor.get_editor_world(), CALIBRATION)
    assert levels.load_level(CALIBRATION)
    for actor in actors.get_all_level_actors():
        if isinstance(actor, (unreal.StaticMeshActor, unreal.PlayerStart)):
            assert actors.destroy_actor(actor)
    floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(4500, 0, -1))
    floor.set_actor_label("E0 - calibration floor")
    floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    floor.set_actor_scale3d(unreal.Vector(120, 80, 1))
    floor.static_mesh_component.set_material(0, unreal.load_asset("/Engine/EngineMaterials/WorldGridMaterial"))
    spawn("/Engine/BasicShapes/Cube", "E0 - 1m cube", (0, 0, 50))
    spawn("/Engine/BasicShapes/Cube", "E0 - 1.8m reference", (300, 0, 90), (.3, .3, 1.8))
    drone = unreal.load_asset("/Game/DroneTrainer/Meshes/SM_TrainingDrone")
    size = drone.get_bounding_box().max - drone.get_bounding_box().min
    spawn(drone.get_path_name(), "E0 - Drone 60x60x20cm", (800, 0, 0),
          (60 / size.x, 60 / size.y, 20 / size.z))
    spawn("/Game/DroneTrainer/Meshes/SM_TrainingGate", "E0 - Gate 4x4m", (2400, 0, 0), (2.6, 2.6, 2.6), 90)
    spawn("/Game/TrainingField/Meshes/Tree_01_Oak/StaticMeshes/Tree_01_Oak", "E0 - Oak", (4200, 0, 0))
    spawn("/Game/TrainingField/Meshes/Bush_01/StaticMeshes/Bush_01", "E0 - Bush", (5400, 0, 0))
    spawn("/Game/TrainingField/Meshes/Tower_01_Observation/StaticMeshes/Tower_01_Observation", "E0 - Tower", (7400, 0, 0))
    assert levels.save_current_level()
    assert levels.load_level(MAP)
    return removed


def inspect(level_path=MAP):
    assert levels.load_level(level_path)
    rows, meshes = [], {}
    for actor in actors.get_all_level_actors():
        origin, extent = actor.get_actor_bounds(False)
        row = {"path": actor.get_path_name(), "label": actor.get_actor_label(),
               "class": actor.get_class().get_path_name(), "transform": str(actor.get_actor_transform()),
               "bounds_origin": [origin.x, origin.y, origin.z],
               "bounds_extent": [extent.x, extent.y, extent.z], "components": []}
        for component in actor.get_components_by_class(unreal.ActorComponent):
            c = {"path": component.get_path_name(), "class": component.get_class().get_path_name()}
            if isinstance(component, unreal.StaticMeshComponent) and component.static_mesh:
                mesh = component.static_mesh
                c.update(mesh=mesh.get_path_name(), materials=[m.get_path_name() if m else None for m in component.get_materials()],
                         collision=str(component.get_collision_enabled()))
                if mesh.get_path_name() not in meshes:
                    data = mesh.get_editor_property("asset_import_data")
                    meshes[mesh.get_path_name()] = {"sources": list(data.extract_filenames()) if data else []}
            row["components"].append(c)
        rows.append(row)
    return {"engine": unreal.SystemLibrary.get_engine_version(), "map": editor.get_editor_world().get_path_name(),
            "actors": rows, "imports": meshes}


def verify_calibration():
    if not assets.does_asset_exist(CALIBRATION):
        return {"status": "not created"}
    assert levels.load_level(CALIBRATION)
    rows = {}
    for actor in actors.get_all_level_actors():
        label = actor.get_actor_label()
        if not label.startswith("E0 - ") or label == "E0 - calibration floor":
            continue
        origin, extent = actor.get_actor_bounds(False)
        size = [2 * extent.x, 2 * extent.y, 2 * extent.z]
        rotation = actor.get_actor_rotation()
        assert abs(rotation.pitch) < .001 and abs(rotation.roll) < .001, label
        assert abs(origin.z - extent.z) < .01, label
        rows[label] = {"size_cm": size, "rotation": str(rotation), "bottom_cm": origin.z - extent.z}
    for label, expected in [("E0 - 1m cube", [100, 100, 100]), ("E0 - Drone 60x60x20cm", [60, 60, 20])]:
        assert all(math.isclose(a, b, abs_tol=.01) for a, b in zip(rows[label]["size_cm"], expected)), label
    assert math.isclose(rows["E0 - Gate 4x4m"]["size_cm"][2], 520, abs_tol=.01)
    assert levels.load_level(MAP)
    return {"status": "passed", "objects": rows}


result = {}
if "-E0Apply" in unreal.SystemLibrary.get_command_line():
    result["moved_to_demo"] = normalize()
result.update(inspect())
if assets.does_asset_exist(DEMO):
    result["demonstration"] = inspect(DEMO)
result["calibration"] = verify_calibration()
out = Path(unreal.Paths.project_saved_dir()) / "E0-check.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(result, ensure_ascii=False, indent=2))
print("E0_CHECK", out, result["calibration"]["status"])
