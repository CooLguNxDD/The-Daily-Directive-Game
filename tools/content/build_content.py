"""Builds the few binary assets the game needs, reproducibly, from text.

Run headless (the editor target must be built):
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<repo>/tools/content/build_content.py" -unattended -AllowCommandletRendering

Creates (or rebuilds):
  /Game/DC/Materials/M_DCFlat  - opaque flat colour: BaseColor = Color, Emissive = Color * Glow (bloom source)
  /Game/DC/Maps/L_City         - an empty level; the city, lights and sky are spawned from C++ at run time
Everything else (city, strikes, enemies, HUD, audio) is code and Config/DefaultGame.ini.
"""
import unreal

MAT_DIR = "/Game/DC/Materials"
MAP_DIR = "/Game/DC/Maps"

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def fresh(path, name, cls, factory):
    full = f"{path}/{name}"
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    return tools.create_asset(name, path, cls, factory)


def build_flat_material():
    mat = fresh(MAT_DIR, "M_DCFlat", unreal.Material, unreal.MaterialFactoryNew())
    color = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.6, 0.55, 0.45, 1.0))
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 250)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 0.0)
    rough = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -600, 400)
    rough.set_editor_property("r", 0.85)
    mul = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 200)
    lib.connect_material_expressions(color, "", mul, "A")
    lib.connect_material_expressions(glow, "", mul, "B")
    lib.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("used_with_skeletal_mesh", False)
    lib.recompile_material(mat)
    eal.save_asset(f"{MAT_DIR}/M_DCFlat")
    unreal.log("build_content: M_DCFlat saved")


def build_map():
    path = f"{MAP_DIR}/L_City"
    if eal.does_asset_exist(path):
        unreal.log("build_content: L_City exists, kept")
        return
    world = tools.create_asset("L_City", MAP_DIR, unreal.World, unreal.WorldFactory())
    eal.save_asset(path)
    unreal.log(f"build_content: L_City saved ({world})")


build_flat_material()
build_map()
unreal.log("build_content: done")
