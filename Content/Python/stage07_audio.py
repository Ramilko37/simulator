import unreal as u
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve().parents[1]
assert not u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
for f in sorted((root/'AssetsSource/Audio/S01-S02').glob('*.wav')):
    path='/Game/TrainingField/Audio/'+f.stem
    assert not u.EditorAssetLibrary.does_asset_exist(path), path
    task=u.AssetImportTask(); task.filename=str(f); task.destination_path='/Game/TrainingField/Audio'; task.destination_name=f.stem; task.automated=True; task.save=True
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]); assert task.imported_object_paths
    sound=u.load_asset(path); sound.set_editor_property('looping',f.stem=='S01_Motors'); assert u.EditorAssetLibrary.save_loaded_asset(sound)
    print('STAGE07_AUDIO_IMPORTED',path)
print('STAGE07_AUDIO_COMPLETE')
