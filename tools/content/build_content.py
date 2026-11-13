"""Builds the few binary assets the game needs, reproducibly, from text.

Run headless (the editor target must be built):
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<repo>/tools/content/build_content.py" -unattended -AllowCommandletRendering

Creates (or rebuilds):
  /Game/DC/Materials/M_DCFlat  - opaque flat colour: BaseColor = Color, Emissive = Color * Glow (bloom source)
  /Game/DC/Maps/L_City         - an empty level; the city, lights and sky are spawned from C++ at run time
  /Game/DC/Materials/M_DCVoxel - voxel meshes: BaseColor = sRGB vertex colour decoded to linear, Emissive = colour *
                                 vertex alpha * GlowScale (lit windows); colours come from the voxel palette in
                                 Config/DefaultGame.ini, so the material holds no colour of its own
Existing assets are kept; set DC_REBUILD=1 to rebuild them.
Everything else (city, strikes, enemies, HUD, audio) is code and Config/DefaultGame.ini.
"""
import os

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


REBUILD = os.environ.get("DC_REBUILD") == "1"


def keep(path):
    if eal.does_asset_exist(path) and not REBUILD:
        unreal.log(f"build_content: {path} exists, kept")
        return True
    return False


def build_flat_material():
    if keep(f"{MAT_DIR}/M_DCFlat"):
        return
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


def build_voxel_material():
    if keep(f"{MAT_DIR}/M_DCVoxel"):
        return
    mat = fresh(MAT_DIR, "M_DCVoxel", unreal.Material, unreal.MaterialFactoryNew())
    vc = lib.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -900, 0)
    # Vertex colours carry sRGB bytes (dark ink tones keep their precision); decode to linear here.
    pw = lib.create_material_expression(mat, unreal.MaterialExpressionPower, -600, 0)
    pw.set_editor_property("const_exponent", 2.2)
    lib.connect_material_expressions(vc, "", pw, "Base")
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, 300)
    glow.set_editor_property("parameter_name", "GlowScale")
    glow.set_editor_property("default_value", 3.0)
    alpha_glow = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -600, 250)
    lib.connect_material_expressions(vc, "A", alpha_glow, "A")
    lib.connect_material_expressions(glow, "", alpha_glow, "B")
    emissive = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 200)
    lib.connect_material_expressions(pw, "", emissive, "A")
    lib.connect_material_expressions(alpha_glow, "", emissive, "B")
    rough = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -600, 450)
    rough.set_editor_property("r", 0.9)
    lib.connect_material_property(pw, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(mat)
    eal.save_asset(f"{MAT_DIR}/M_DCVoxel")
    unreal.log("build_content: M_DCVoxel saved")


def build_map():
    path = f"{MAP_DIR}/L_City"
    if eal.does_asset_exist(path):
        unreal.log("build_content: L_City exists, kept")
        return
    world = tools.create_asset("L_City", MAP_DIR, unreal.World, unreal.WorldFactory())
    eal.save_asset(path)
    unreal.log(f"build_content: L_City saved ({world})")


build_flat_material()
build_voxel_material()
build_map()
unreal.log("build_content: done")
