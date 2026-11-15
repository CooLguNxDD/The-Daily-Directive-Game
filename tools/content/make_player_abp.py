"""R16 aim pose (advisor a-23): makes /Game/DC/Anim/ABP_DCPlayer, a duplicate of the template ABP_Unarmed whose parent
class is the C++ UDCAnimInstance (its native proxy bends the spine by the replicated aim pitch after the graph runs).
The graph itself is not edited. Reproducible from text; run headless after the editor target is built:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<repo>/tools/content/make_player_abp.py" -unattended
Set DC_REBUILD=1 to rebuild an existing ABP_DCPlayer. Prints DCABP lines.
"""
import os

import unreal

SRC = "/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"
DST_DIR = "/Game/DC/Anim"
DST = DST_DIR + "/ABP_DCPlayer"
PARENT = "/Script/Epic_AI_Mega_Jam2.DCAnimInstance"

lib = unreal.EditorAssetLibrary
if lib.does_asset_exist(DST) and os.environ.get("DC_REBUILD") == "1":
    lib.delete_asset(DST)
if not lib.does_asset_exist(DST):
    if not lib.does_directory_exist(DST_DIR):
        lib.make_directory(DST_DIR)
    if not lib.duplicate_asset(SRC, DST):
        raise RuntimeError("DCABP duplicate failed: " + SRC)
bp = unreal.load_asset(DST)
parent = unreal.load_class(None, PARENT)
if parent is None:
    raise RuntimeError("DCABP parent class missing (build the editor target first): " + PARENT)
unreal.BlueprintEditorLibrary.reparent_blueprint(bp, parent)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
lib.save_asset(DST, only_if_is_dirty=False)
gen = unreal.BlueprintEditorLibrary.generated_class(bp)
print("DCABP made %s parent=%s generated=%s" % (DST, PARENT, gen.get_path_name() if gen else None))
