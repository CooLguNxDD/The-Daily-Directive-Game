"""Builds the few binary assets the game needs, reproducibly, from text.

Run headless (the editor target must be built):
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<repo>/tools/content/build_content.py" -unattended -AllowCommandletRendering

Creates (or rebuilds):
  /Game/DC/Materials/M_DCFlat  - opaque flat colour: BaseColor = Color, Emissive = Color * Glow (bloom source)
  /Game/DC/Materials/M_DCFlatUnlit - unlit flat colour: Emissive = Color * Glow (danger red stays palette red)
  /Game/DC/Maps/L_City         - an empty level; the city, lights and sky are spawned from C++ at run time
  /Game/DC/Materials/M_DCVoxel - voxel meshes: BaseColor = sRGB vertex colour decoded to linear, Emissive = colour *
                                 vertex alpha * GlowScale (lit windows); colours come from the voxel palette in
                                 Config/DefaultGame.ini, so the material holds no colour of its own
  /Game/DC/Materials/M_DCInkOutline - post material: ink lines where depth or normal jumps (Sobel-style cross of
                                 four taps); Strength comes from the post stack's Outline field, the rest from Look tuning
  /Game/DC/Fonts/FF_*            - OFL font faces (R7) from Content/DC/Fonts/Source/*.ttf, inline data; the game
                                 builds runtime fonts from them by role (Assets.FontFaces in Config/DefaultGame.ini)
Existing assets are kept; set DC_REBUILD=1 to rebuild them (DC_REBUILD_ONLY=M_DCInkOutline rebuilds one).
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
REBUILD_ONLY = os.environ.get("DC_REBUILD_ONLY", "")


def keep(path):
    if eal.does_asset_exist(path) and not REBUILD and not (REBUILD_ONLY and path.endswith("/" + REBUILD_ONLY)):
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


def build_flat_unlit_material():
    """Unlit flat colour (danger red beam and ring): Emissive = Color * Glow, no lighting on it."""
    if keep(f"{MAT_DIR}/M_DCFlatUnlit"):
        return
    mat = fresh(MAT_DIR, "M_DCFlatUnlit", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    color = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.687, 0.019, 0.011, 1.0))
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 250)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 1.0)
    mul = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 100)
    lib.connect_material_expressions(color, "", mul, "A")
    lib.connect_material_expressions(glow, "", mul, "B")
    lib.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    lib.recompile_material(mat)
    eal.save_asset(f"{MAT_DIR}/M_DCFlatUnlit")
    unreal.log("build_content: M_DCFlatUnlit saved")


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


OUTLINE_HLSL = r"""
float2 Px = float2(Thickness, Thickness);
float Dc = SceneTextureFetch(PPI_SceneDepth, float2(0, 0)).r;
float3 Nc = SceneTextureFetch(PPI_WorldNormal, float2(0, 0)).rgb;
float Edge = 0;
float2 Taps[4] = { float2(Px.x, 0), float2(-Px.x, 0), float2(0, Px.y), float2(0, -Px.y) };
for (int i = 0; i < 4; ++i)
{
    float D = SceneTextureFetch(PPI_SceneDepth, Taps[i]).r;
    float3 N = SceneTextureFetch(PPI_WorldNormal, Taps[i]).rgb;
    // Only the nearer side of a depth step inks, so silhouettes stay one line wide.
    Edge = max(Edge, saturate(((D - Dc) / max(Dc, 1.0) - DepthThreshold) * 25.0));
    Edge = max(Edge, saturate((1.0 - dot(N, Nc) - NormalThreshold) * 4.0));
}
Edge *= saturate(1.0 - Dc / max(FadeDistance, 1.0)) * saturate(Strength);
float3 Scene = SceneTextureFetch(PPI_PostProcessInput0, float2(0, 0)).rgb;
return lerp(Scene, InkColor.rgb * min(Scene, 1.0), Edge);
"""


def build_outline_material():
    if keep(f"{MAT_DIR}/M_DCInkOutline"):
        return
    mat = fresh(MAT_DIR, "M_DCInkOutline", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)
    custom = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property("code", OUTLINE_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "InkOutline")
    names = ["Thickness", "DepthThreshold", "NormalThreshold", "FadeDistance", "Strength", "InkColor",
             "UsesDepth", "UsesNormal", "UsesScene"]
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    defaults = {"Thickness": 1.5, "DepthThreshold": 0.03, "NormalThreshold": 0.3, "FadeDistance": 25000.0,
                "Strength": 0.0}
    for i, (n, v) in enumerate(defaults.items()):
        p = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, i * 90)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", v)
        lib.connect_material_expressions(p, "", custom, n)
    ink = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, 480)
    ink.set_editor_property("parameter_name", "InkColor")
    ink.set_editor_property("default_value", unreal.LinearColor(0.008, 0.008, 0.01, 1.0))
    lib.connect_material_expressions(ink, "", custom, "InkColor")
    # Scene texture nodes register the depth, normal and scene colour reads the HLSL makes.
    for i, (pin, sid) in enumerate([("UsesDepth", unreal.SceneTextureId.PPI_SCENE_DEPTH),
                                    ("UsesNormal", unreal.SceneTextureId.PPI_WORLD_NORMAL),
                                    ("UsesScene", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)]):
        st = lib.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -700, 600 + i * 120)
        st.set_editor_property("scene_texture_id", sid)
        lib.connect_material_expressions(st, "Color", custom, pin)
    lib.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    eal.save_asset(f"{MAT_DIR}/M_DCInkOutline")
    unreal.log("build_content: M_DCInkOutline saved")


ART_DIR = "/Game/DC/Art"
ART_SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Content", "DC", "Art", "Source")

# Generated art (R11, listed in ASSETS.md): source PNG beside the asset, imported as UI textures.
TEXTURES = {
    "T_MenuKeyArt": "menu-keyart.png",
}


def import_textures():
    for name, file in TEXTURES.items():
        path = f"{ART_DIR}/{name}"
        src = os.path.normpath(os.path.join(ART_SOURCE, file))
        if keep(path) or not os.path.exists(src):
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", src)
        task.set_editor_property("destination_path", ART_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tools.import_asset_tasks([task])
        tex = unreal.load_asset(path)
        if tex:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
            tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            eal.save_asset(path)
            unreal.log(f"build_content: {name} imported from {file}")


FONT_DIR = "/Game/DC/Fonts"
FONT_SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Content", "DC", "Fonts", "Source")

# OFL faces (licences beside them in Source/, listed in ASSETS.md).
FONTS = {
    "FF_UnifrakturMaguntia": "UnifrakturMaguntia-Book.ttf",
    "FF_Anton": "Anton-Regular.ttf",
    "FF_Oswald": "Oswald[wght].ttf",
    "FF_BlackOpsOne": "BlackOpsOne-Regular.ttf",
}


def import_fonts():
    for name, file in FONTS.items():
        path = f"{FONT_DIR}/{name}"
        src = os.path.normpath(os.path.join(FONT_SOURCE, file))
        if keep(path) or not os.path.exists(src):
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", src)
        task.set_editor_property("destination_path", FONT_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        task.set_editor_property("factory", unreal.FontFileImportFactory())
        tools.import_asset_tasks([task])
        face = unreal.load_asset(path)
        if not isinstance(face, unreal.FontFace):
            unreal.log_error(f"build_content: {file} did not import as a FontFace ({face})")
            continue
        face.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
        eal.save_asset(path)
        unreal.log(f"build_content: {name} imported from {file}")


def build_map():
    path = f"{MAP_DIR}/L_City"
    if eal.does_asset_exist(path):
        unreal.log("build_content: L_City exists, kept")
        return
    world = tools.create_asset("L_City", MAP_DIR, unreal.World, unreal.WorldFactory())
    eal.save_asset(path)
    unreal.log(f"build_content: L_City saved ({world})")


build_flat_material()
build_flat_unlit_material()
build_voxel_material()
build_outline_material()
import_textures()
import_fonts()
build_map()
unreal.log("build_content: done")
