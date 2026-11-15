"""Builds the few binary assets the game needs, reproducibly, from text.

Run headless (the editor target must be built):
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<repo>/tools/content/build_content.py" -unattended -AllowCommandletRendering

Creates (or rebuilds):
  /Game/DC/Materials/M_DCFlat  - opaque flat colour: BaseColor = Color, Emissive = Color * Glow (bloom source)
  /Game/DC/Materials/M_DCFlatUnlit - unlit flat colour: Emissive = Color * Glow (danger red stays palette red)
  /Game/DC/Maps/L_City         - an empty level; the city, lights and sky are spawned from C++ at run time
  /Game/DC/Materials/M_DCVoxel - voxel meshes: BaseColor = sRGB vertex colour decoded to linear with a per-voxel
                                 jitter and edge bevel (VoxelCm per grid size), darkened by vertex alpha *
                                 GlowBodyDark (lit glass, neon), Emissive = colour * vertex alpha * GlowScale
                                 (lit windows, signs) + a faint moon-facing form floor (FormFloor, FormSky,
                                 FormGround, MoonDir); colours come from the voxel palette in
                                 Config/DefaultGame.ini, so the material holds no colour of its own
  /Game/DC/Materials/M_DCVoxelDissolve, M_DCFlatDissolve - R15 masked twins of M_DCVoxel / M_DCFlat for blocks and
                                 crumbs being cleared away: a world-space value-noise dissolve with a faint warm edge
                                 (DissolveEdgeColor, DissolveEdgeGlow, DissolveEdge, DissolveNoiseCm). The amount comes
                                 from custom primitive data 0 (pieces) or per-instance custom data 0 (crumb instances)
  /Game/DC/Materials/M_DCVoxelUnit - R20 twin of M_DCVoxel for animated units (h-anim, advisor a-28): World Position
                                 Offset turns each vertex's segment (UV1.x) about its joint (UV1.y, UV2.xy, cm,
                                 model space) by the quaternion in custom primitive data 1 + 4 * (segment - 1)
                                 (UDCUnitAnimator writes them); segment 0 and a zero quaternion stay put. Never on
                                 M_DCVoxel itself (a WPO city would invalidate shadow pages every frame)
  /Game/DC/Materials/M_DCInkOutline - post material: ink lines where depth or normal jumps (Sobel-style cross of
                                 four taps); Strength comes from the post stack's Outline field, the rest from Look tuning
  /Game/DC/Fonts/FF_*            - OFL font faces (R7) from Content/DC/Fonts/Source/*.ttf, inline data; the game
                                 builds runtime fonts from them by role (Assets.FontFaces in Config/DefaultGame.ini)
  /Game/DC/Audio/S_Music_*      - R14 looping music from Content/DC/Audio/Source/<Id>.wav (tools/audio/tracks.json rows)
  /Game/DC/Audio/S_Sfx_*        - R13 remixed one-shot cues from Content/DC/Audio/Source/Sfx_<Cue>.wav (tools/audio/sfx.json)
Existing assets are kept; set DC_REBUILD=1 to rebuild them (DC_REBUILD_ONLY=M_DCInkOutline,T_MenuKeyArt rebuilds those).
Everything else (city, strikes, enemies, HUD, SFX) is code and Config/DefaultGame.ini.
"""
import json
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
    if eal.does_asset_exist(path) and not REBUILD and not (REBUILD_ONLY and path.split("/")[-1] in REBUILD_ONLY.split(",")):
        unreal.log(f"build_content: {path} exists, kept")
        return True
    return False


DISSOLVE_HLSL = """
float3 p = WorldPos / max(NoiseCm, 1.0);
float3 i = floor(p);
float3 f = frac(p);
f = f * f * (3.0 - 2.0 * f);
float n = 0.0;
for (int c = 0; c < 8; c++)
{
    float3 o = float3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
    float h = frac(sin(dot(i + o, float3(127.1, 311.7, 74.7))) * 43758.5453);
    float3 w = lerp(1.0 - f, f, o);
    n += h * w.x * w.y * w.z;
}
float d = saturate(Dissolve);
float m = n - d * 1.02;
float mask = (d <= 0.0 || m >= 0.0) ? 1.0 : 0.0;
float edge = (d > 0.0 && m >= 0.0) ? saturate(1.0 - m / max(EdgeWidth, 0.001)) : 0.0;
return Which > 0.5 ? edge : mask;
"""


def add_dissolve(mat, emissive_src, dissolve_src, x, y):
    """R15: masked noise dissolve on a material whose emissive output is emissive_src; dissolve_src drives the amount."""
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    wp = lib.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, x - 300, y)
    prms = []
    for i, (n, v) in enumerate([("DissolveNoiseCm", 18.0), ("DissolveEdge", 0.12)]):
        prm = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x - 300, y + 100 + i * 90)
        prm.set_editor_property("parameter_name", n)
        prm.set_editor_property("default_value", v)
        prms.append(prm)
    outs = []
    # Two single-output Custom nodes (mask, edge): no ComponentMask (its channel flags did not take from Python and a
    # float2 reached a Multiply: "Failed to compile Material", default material in game).
    for k, (desc, which) in enumerate([("DissolveMask", 0.0), ("DissolveEdge", 1.0)]):
        custom = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y + k * 160)
        custom.set_editor_property("code", DISSOLVE_HLSL)
        custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        custom.set_editor_property("description", desc)
        inputs = []
        for n in ["WorldPos", "Dissolve", "NoiseCm", "EdgeWidth", "Which"]:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            inputs.append(ci)
        custom.set_editor_property("inputs", inputs)
        lib.connect_material_expressions(wp, "", custom, "WorldPos")
        lib.connect_material_expressions(dissolve_src, "", custom, "Dissolve")
        lib.connect_material_expressions(prms[0], "", custom, "NoiseCm")
        lib.connect_material_expressions(prms[1], "", custom, "EdgeWidth")
        w = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, x - 150, y + k * 160 + 60)
        w.set_editor_property("r", which)
        lib.connect_material_expressions(w, "", custom, "Which")
        outs.append(custom)
    mask, edge = outs
    lib.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    col = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y + 300)
    col.set_editor_property("parameter_name", "DissolveEdgeColor")
    col.set_editor_property("default_value", unreal.LinearColor(1.0, 0.45, 0.15, 1.0))
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y + 420)
    glow.set_editor_property("parameter_name", "DissolveEdgeGlow")
    glow.set_editor_property("default_value", 3.0)
    # Emissive = the material's own emissive + the warm edge, in one Custom node (no Multiply nodes: a Multiply on the
    # vector parameter's output failed with "float2 and float3" in both twins).
    emi = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, x + 450, y + 200)
    emi.set_editor_property("code", "return Base.rgb + EdgeColor.rgb * (Glow * Edge);")
    emi.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    emi.set_editor_property("description", "DissolveEmissive")
    einputs = []
    for n in ["Base", "EdgeColor", "Glow", "Edge"]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        einputs.append(ci)
    emi.set_editor_property("inputs", einputs)
    lib.connect_material_expressions(emissive_src, "", emi, "Base")
    lib.connect_material_expressions(col, "", emi, "EdgeColor")
    lib.connect_material_expressions(glow, "", emi, "Glow")
    lib.connect_material_expressions(edge, "", emi, "Edge")
    lib.connect_material_property(emi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def build_flat_material(name="M_DCFlat", dissolve=False):
    if keep(f"{MAT_DIR}/{name}"):
        return
    mat = fresh(MAT_DIR, name, unreal.Material, unreal.MaterialFactoryNew())
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
    if dissolve:
        # Crumb instances: the amount is per-instance custom data 0 (DCCrumbField sets it while a crumb clears).
        amount = lib.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -600, 600)
        amount.set_editor_property("data_index", 0)
        add_dissolve(mat, mul, amount, -300, 600)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("used_with_skeletal_mesh", False)
    lib.recompile_material(mat)
    eal.save_asset(f"{MAT_DIR}/{name}")
    unreal.log(f"build_content: {name} saved")


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


VOXEL_HLSL = r"""
// Voxel readability (V4.2, advisor CP2): each voxel gets a small value jitter from its cell id and a dark bevel at its
// edges, in grid-local space (LocalPos, cm) so the lines sit on the real voxel grid. Both fade as a voxel shrinks
// under a few pixels (no moire at distance).
float3 P = LocalPos / max(VoxelCm, 1.0);
float3 N = abs(Normal);
float2 UV = N.x > 0.5 ? P.yz : (N.y > 0.5 ? P.xz : P.xy);
float3 Cell = floor(P - Normal * 0.5);
float H = frac(sin(dot(Cell, float3(12.9898, 78.233, 37.719))) * 43758.5453);
float2 F = frac(UV);
float2 D = min(F, 1.0 - F);
float Edge = 1.0 - saturate(min(D.x, D.y) / max(EdgeWidth, 0.01));
float Px = length(fwidth(UV));
float Fade = saturate(1.0 - (Px - 0.12) / 0.3);
float Shade = 1.0 - (Edge * EdgeDark + (H - 0.5) * Jitter) * Fade;
float Wet = saturate(Normal.z) * Wetness;
return Base * Shade * (1.0 - 0.38 * Wet);
"""

VOXEL_ROUGH_HLSL = r"""
// Surface wetness (R18): up faces gloss as they get wet (rain), walls stay matte.
return lerp(0.9, 0.2, saturate(Normal.z) * Wetness); // R25: wet streets mirror the neon (the night sky is dark)
"""


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


VOXEL_FORM_HLSL = r"""
// Form floor (advisor CP16, critic V20.5): a faint moon-and-sky term on the shaded albedo, so voxel faces that turn
// toward the moon separate from faces turned away even with no light on them. Values from FDCVoxelTuning (Form*).
float K = saturate(dot(normalize(Normal), normalize(MoonDir.xyz)) * 0.5 + 0.5);
return Albedo * lerp(Ground.rgb, Sky.rgb, K * K) * FormFloor;
"""


UNIT_WPO_HLSL = """
// R20: rotate this vertex about its segment's joint by the segment's quaternion (custom primitive data).
float4 Qs[8] = { float4(0, 0, 0, 0), float4(Q1.xyz, W1.x), float4(Q2.xyz, W2.x), float4(Q3.xyz, W3.x), float4(Q4.xyz, W4.x), float4(Q5.xyz, W5.x), float4(Q6.xyz, W6.x), float4(Q7.xyz, W7.x) };
int s = clamp((int)round(Seg.x), 0, 7);
float4 q = Qs[s];
float3 j = float3(Seg.y, Joint.x, Joint.y);
float3 v = LocalPos - j;
float3 t = 2.0 * cross(q.xyz, v);
float3 r = v + q.w * t + cross(q.xyz, t);
return r - v;
"""


def add_unit_wpo(mat, x, y):
    """R20: World Position Offset that turns each segment about its joint (see UNIT_WPO_HLSL)."""
    custom = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("code", UNIT_WPO_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "UnitSegments")
    names = ["LocalPos", "Seg", "Joint"] + [f"{p}{k}" for k in range(1, 8) for p in ("Q", "W")]
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    lp = lib.create_material_expression(mat, unreal.MaterialExpressionLocalPosition, x - 400, y)
    try:  # the rest position, before any offset (the WPO is being computed from it)
        lp.set_editor_property("included_offsets", unreal.PositionIncludedOffsets.EXCLUDE_OFFSETS)
    except Exception as e:  # older python enum names: keep the default
        unreal.log(f"build_content: M_DCVoxelUnit local position offsets left default ({e})")
    lib.connect_material_expressions(lp, "", custom, "LocalPos")
    for idx, pin in ((1, "Seg"), (2, "Joint")):
        uv = lib.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, x - 400, y + 80 * idx)
        uv.set_editor_property("coordinate_index", idx)
        lib.connect_material_expressions(uv, "", custom, pin)
    for k in range(1, 8):
        q = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x - 400, y + 200 + 120 * k)
        q.set_editor_property("parameter_name", f"Seg{k}")
        q.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        q.set_editor_property("use_custom_primitive_data", True)
        q.set_editor_property("primitive_data_index", 1 + 4 * (k - 1))
        lib.connect_material_expressions(q, "", custom, f"Q{k}")
        lib.connect_material_expressions(q, "A", custom, f"W{k}")
    world = lib.create_material_expression(mat, unreal.MaterialExpressionTransform, x + 250, y)
    world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    lib.connect_material_expressions(custom, "", world, "")
    lib.connect_material_property(world, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


def build_voxel_material(name="M_DCVoxel", dissolve=False, unit=False):
    if keep(f"{MAT_DIR}/{name}"):
        return
    mat = fresh(MAT_DIR, name, unreal.Material, unreal.MaterialFactoryNew())
    vc = lib.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -1100, 0)
    # Vertex colours carry sRGB bytes (dark ink tones keep their precision); decode to linear here.
    pw = lib.create_material_expression(mat, unreal.MaterialExpressionPower, -850, 0)
    pw.set_editor_property("const_exponent", 2.2)
    lib.connect_material_expressions(vc, "", pw, "Base")
    custom = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, -550, 0)
    custom.set_editor_property("code", VOXEL_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "VoxelEdges")
    inputs = []
    for n in ["Base", "LocalPos", "Normal", "VoxelCm", "EdgeWidth", "EdgeDark", "Jitter", "Wetness"]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    lib.connect_material_expressions(pw, "", custom, "Base")
    lp = lib.create_material_expression(mat, unreal.MaterialExpressionLocalPosition, -850, 150)
    lib.connect_material_expressions(lp, "", custom, "LocalPos")
    nrm = lib.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -850, 250)
    lib.connect_material_expressions(nrm, "", custom, "Normal")
    for i, (n, v) in enumerate([("VoxelCm", 10.0), ("EdgeWidth", 0.08), ("EdgeDark", 0.35), ("Jitter", 0.12)]):
        prm = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -850, 350 + i * 90)
        prm.set_editor_property("parameter_name", n)
        prm.set_editor_property("default_value", v)
        lib.connect_material_expressions(prm, "", custom, n)
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -850, 750)
    glow.set_editor_property("parameter_name", "GlowScale")
    glow.set_editor_property("default_value", 3.0)
    alpha_glow = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -550, 650)
    lib.connect_material_expressions(vc, "A", alpha_glow, "A")
    lib.connect_material_expressions(glow, "", alpha_glow, "B")
    emissive = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 400)
    lib.connect_material_expressions(pw, "", emissive, "A")
    lib.connect_material_expressions(alpha_glow, "", emissive, "B")
    wet = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -850, 950)
    wet.set_editor_property("parameter_name", "Wetness")
    wet.set_editor_property("default_value", 0.0)
    lib.connect_material_expressions(wet, "", custom, "Wetness")
    rough = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, -550, 850)
    rough.set_editor_property("code", VOXEL_ROUGH_HLSL)
    rough.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    rough.set_editor_property("description", "Wetness")
    rinputs = []
    for n in ["Normal", "Wetness"]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        rinputs.append(ci)
    rough.set_editor_property("inputs", rinputs)
    lib.connect_material_expressions(nrm, "", rough, "Normal")
    lib.connect_material_expressions(wet, "", rough, "Wetness")
    # Lit glass and neon (R25 look sweep): a glowing voxel's body darkens with its glow, so signs and windows read as
    # light sources (emission + bloom halo) instead of paint the moon and sign lights bleach to pastel.
    body_dark = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -850, 1050)
    body_dark.set_editor_property("parameter_name", "GlowBodyDark")
    body_dark.set_editor_property("default_value", 0.9)
    glow_body = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -550, 1050)
    lib.connect_material_expressions(vc, "A", glow_body, "A")
    lib.connect_material_expressions(body_dark, "", glow_body, "B")
    glow_sat = lib.create_material_expression(mat, unreal.MaterialExpressionSaturate, -400, 1050)
    lib.connect_material_expressions(glow_body, "", glow_sat, "")
    glow_keep = lib.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -300, 1050)
    lib.connect_material_expressions(glow_sat, "", glow_keep, "")
    base_out = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 0)
    lib.connect_material_expressions(custom, "", base_out, "A")
    lib.connect_material_expressions(glow_keep, "", base_out, "B")
    lib.connect_material_property(base_out, "", unreal.MaterialProperty.MP_BASE_COLOR)
    form = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 1300)
    form.set_editor_property("code", VOXEL_FORM_HLSL)
    form.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    form.set_editor_property("description", "FormFloor")
    finputs = []
    for n in ["Albedo", "Normal", "MoonDir", "Sky", "Ground", "FormFloor"]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        finputs.append(ci)
    form.set_editor_property("inputs", finputs)
    lib.connect_material_expressions(base_out, "", form, "Albedo")
    lib.connect_material_expressions(nrm, "", form, "Normal")
    for i, (n, v) in enumerate([("MoonDir", unreal.LinearColor(-0.4, 0.3, 0.85, 0.0)),
                                ("FormSky", unreal.LinearColor(0.32, 0.4, 0.8, 1.0)),
                                ("FormGround", unreal.LinearColor(0.05, 0.05, 0.08, 1.0))]):
        vp = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -850, 1300 + i * 110)
        vp.set_editor_property("parameter_name", n)
        vp.set_editor_property("default_value", v)
        lib.connect_material_expressions(vp, "", form, {"FormSky": "Sky", "FormGround": "Ground"}.get(n, n))
    ff = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -850, 1650)
    ff.set_editor_property("parameter_name", "FormFloor")
    ff.set_editor_property("default_value", 0.0)
    lib.connect_material_expressions(ff, "", form, "FormFloor")
    emissive_all = lib.create_material_expression(mat, unreal.MaterialExpressionAdd, -100, 400)
    lib.connect_material_expressions(emissive, "", emissive_all, "A")
    lib.connect_material_expressions(form, "", emissive_all, "B")
    lib.connect_material_property(emissive_all, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    if dissolve:
        # Clearing pieces: the amount is custom primitive data 0 (ADCVoxelCity sets it per piece while it fades).
        amount = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -850, 1800)
        amount.set_editor_property("parameter_name", "Dissolve")
        amount.set_editor_property("use_custom_primitive_data", True)
        amount.set_editor_property("primitive_data_index", 0)
        add_dissolve(mat, emissive_all, amount, -400, 1800)
    if unit:
        add_unit_wpo(mat, -500, 2200)
    lib.recompile_material(mat)
    eal.save_asset(f"{MAT_DIR}/{name}")
    unreal.log(f"build_content: {name} saved")


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
    "T_Card_sundrop": "card-sundrop.png",
    "T_Card_singularity": "card-singularity.png",
    "T_Card_decoy": "card-decoy.png",
    "T_Card_strobe": "card-strobe.png",
    "T_Card_fisheye": "card-fisheye.png",
    "T_Card_cursed": "card-cursed.png",
    "T_Card_domino": "card-domino.png",
    "T_Card_eclipse": "card-eclipse.png",
    "T_Card_rogue": "card-rogue.png",
    "T_Card_gunrun": "card-gunrun.png",
    "T_Card_cluster": "card-cluster.png",
    "T_Card_bunker": "card-bunker.png",
    "T_Card_carpet": "card-carpet.png",
    "T_Card_drone": "card-drone.png",
    "T_Card_turret": "card-turret.png",
    "T_Card_reinforce": "card-reinforce.png",
    "T_Card_deadman": "card-deadman.png",
    "T_PaperGrain": "tex-paper.png",
    "T_StampGrit": "tex-stampgrit.png",
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
            if name.startswith("T_Card_"):
                tex.set_editor_property("max_texture_size", 256)  # draft card art is drawn under 200 px
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


AUDIO_DIR = "/Game/DC/Audio"
AUDIO_SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Content", "DC", "Audio", "Source")
TRACKS_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "audio", "tracks.json")


def import_music():
    """R14: every tools/audio/tracks.json row whose loop wav exists (made by tools/audio/make_loop.py) becomes a
    looping USoundWave S_Music_<Id>; Config/DefaultGame.ini +MusicTracks points at it."""
    with open(TRACKS_JSON, encoding="utf-8") as f:
        rows = json.load(f)["tracks"]
    for row in rows:
        name = f"S_Music_{row['Id']}"
        path = f"{AUDIO_DIR}/{name}"
        src = os.path.normpath(os.path.join(AUDIO_SOURCE, f"{row['Id']}.wav"))
        if keep(path) or not os.path.exists(src):
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", src)
        task.set_editor_property("destination_path", AUDIO_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tools.import_asset_tasks([task])
        wave = unreal.load_asset(path)
        if not isinstance(wave, unreal.SoundWave):
            unreal.log_error(f"build_content: {src} did not import as a SoundWave ({wave})")
            continue
        wave.set_editor_property("looping", True)
        wave.set_editor_property("virtualization_mode", unreal.VirtualizationMode.PLAY_WHEN_SILENT)
        eal.save_asset(path)
        unreal.log(f"build_content: {name} imported from {src}")


SFX_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "audio", "sfx.json")


def import_sfx():
    """R13 remix: every tools/audio/sfx.json row whose master exists (tools/audio/make_sfx.py) becomes a one-shot
    USoundWave S_Sfx_<Cue>; a +CueMix row with Sound= points the cue at it."""
    with open(SFX_JSON, encoding="utf-8") as f:
        rows = json.load(f)["sfx"]
    for row in rows:
        name = f"S_Sfx_{row['Cue']}"
        path = f"{AUDIO_DIR}/{name}"
        src = os.path.normpath(os.path.join(AUDIO_SOURCE, f"Sfx_{row['Cue']}.wav"))
        if keep(path) or not os.path.exists(src):
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", src)
        task.set_editor_property("destination_path", AUDIO_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tools.import_asset_tasks([task])
        wave = unreal.load_asset(path)
        if not isinstance(wave, unreal.SoundWave):
            unreal.log_error(f"build_content: {src} did not import as a SoundWave ({wave})")
            continue
        wave.set_editor_property("looping", False)
        eal.save_asset(path)
        unreal.log(f"build_content: {name} imported from {src}")


def build_map():
    path = f"{MAP_DIR}/L_City"
    if eal.does_asset_exist(path):
        unreal.log("build_content: L_City exists, kept")
        return
    world = tools.create_asset("L_City", MAP_DIR, unreal.World, unreal.WorldFactory())
    eal.save_asset(path)
    unreal.log(f"build_content: L_City saved ({world})")


build_flat_material()
build_flat_material("M_DCFlatDissolve", dissolve=True)
build_flat_unlit_material()
build_voxel_material()
build_voxel_material("M_DCVoxelDissolve", dissolve=True)
build_voxel_material("M_DCVoxelUnit", unit=True)
build_outline_material()
import_textures()
import_fonts()
import_music()
import_sfx()
build_map()
unreal.log("build_content: done")
