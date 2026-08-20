# PBR Material Workflow

This fork replaces RBDOOM-3-BFG's original **Roughness/Metallic** (Unreal-style)
PBR workflow with a **Specular/Gloss** (CryEngine-style) workflow as the
default interpretation of PBR materials. Classic Doom 3 Blinn-Phong content
still works, and is automatically improved by an approximation pass ("Kenny
PBR") rather than requiring reauthoring.

If you're used to the upstream RBDOOM-3-BFG PBR docs: everywhere they say
"Roughness/Metallic", read "Specular/Gloss" instead. The material keywords
(`specularmap`, `rmaomap`, `normalmap`, etc.) and the `_rmao` filename
convention are unchanged - only what the engine *does* with that data changed.

## The three rendering paths

Every material's specular stage is interpreted one of three ways at render
time, decided automatically per-material with no extra `.mtr` keywords
needed:

| Path | When it's used | What it does |
|------|-----------------|--------------|
| **Specular/Gloss PBR** (new default) | Specular texture's filename contains `_rmao` or `_rmaod` | Reads the texture directly as PBR data - no guessing. RGB = specular color (F0), alpha = gloss. |
| **Kenny PBR converter** (legacy approximation) | Any other specular texture (classic Doom 3 spec maps, no special filename) | Approximates plausible PBR roughness/specular values *from* a traditional Blinn-Phong specular texture. Makes old content look noticeably better under modern lighting without touching the source assets. |
| **No specular stage at all** | Material only has `diffusemap`/`bumpmap` | Falls back to the engine's default specular texture, which resolves through the Kenny PBR path above. |

The routing check lives in `idMaterial::ParseStage` (`neo/renderer/Material.cpp`)
and looks for the `_rmao`/`_rmaod` substring in the specular image's filename.
The actual shading math for all three paths lives in `neo/shaders/BRDF.inc.hlsl`
and is consumed by the `USE_PBR` / `KENNY_PBR` branches in each lighting
shader (`interaction.ps.hlsl`, `interactionSM.ps.hlsl`,
`ambient_lighting_IBL.ps.hlsl`, `ambient_lightgrid_IBL.ps.hlsl`).

---

## Path 1: Specular/Gloss PBR (new default)

This is what you want for properly authored PBR content - Substance
Painter/Designer exports, or any texture set with a real specular color map
and a gloss/smoothness map, matching the same convention tools like Call of
Duty: Black Ops III's asset pipeline use.

**Specular map (RGBA) channel layout:**

| Channel | Contains |
|---------|----------|
| Red     | Specular color (F0) - red component |
| Green   | Specular color (F0) - green component |
| Blue    | Specular color (F0) - blue component |
| Alpha   | Gloss (smoothness). Roughness is derived as `1.0 - gloss`. |

No approximation happens on this path - the texture directly *is* the PBR
data, so the values you author are the values that get lit.

**Example material:**

```
models/mapobjects/materialorb/orb
{
  qer_editorimage   models/mapobjects/pbr/materialorb/substance/metal04_basecolor.png

  basecolormap      models/mapobjects/pbr/materialorb/substance/metal04_basecolor.png
  normalmap         models/mapobjects/pbr/materialorb/substance/metal04_normal.png
  specularmap       models/mapobjects/pbr/materialorb/substance/metal04_rmao.png
}
```

The `specularmap` (or `rmaomap` - they're interchangeable aliases for the
same material stage) texture file must have `_rmao` or `_rmaod` somewhere in
its filename for the engine to route it through this path. This is a legacy
naming convention (it originally stood for "Roughness/Metal/AO" under the old
workflow) that's been repurposed to mean "this texture directly encodes PBR
data, don't approximate it" - the letters are a bit of a historical artifact
at this point, but changing the convention wasn't worth the churn across
existing content.

---

## Path 2: Kenny PBR converter (legacy Blinn-Phong content)

This is what automatically happens to *any* material using a classic Doom 3
specular texture (the traditional `_s.tga`-style specular/gloss map, or any
specular filename that doesn't contain `_rmao`/`_rmaod`). You don't need to
do anything to opt into this - it's the fallback, and it's a meaningful
visual upgrade over flat Blinn-Phong even though the source texture was never
designed with PBR in mind.

**Example material (completely unmodified classic Doom 3 syntax):**

```
textures/base_wall/snpanel2rust
{
  qer_editorimage   textures/base_wall/snpanel2rust.tga

  bumpmap           textures/base_wall/snpanel2_local.tga
  diffusemap        textures/base_wall/snpanel2rust_d.tga
  specularmap       textures/base_wall/snpanel2rust_s.tga
}
```

Under the hood this calls `PBRFromSpecmap()` in `BRDF.inc.hlsl`, which
estimates a specular color (F0) and roughness from the legacy specular
texture's intensity/contrast. It's a heuristic, not a real conversion - there
was never real PBR data in these textures to recover - but it produces a
believable, modernized look without any reauthoring work. If you have the
option to author real specular+gloss textures for a piece of content, Path 1
above will always look more accurate.

---

## makeMaterials: automatic material generation from loose textures

The in-engine console command `makeMaterials <folder>` scans a folder of
loose texture files and generates a `.mtr` declaration automatically, based
on filename suffixes. It decides which of the two paths above to route a
material through based on what source textures it actually finds:

**Routes to the new Specular/Gloss PBR workflow (Path 1) when it finds:**
- A base color / albedo texture
- A specular color texture (`_spec`, `_specular`, `_specularmap`, or
  `_reflection` suffix)
- Gloss data, from either:
  - A combined normal+gloss texture (`_normal_gloss`, `_nrm_gloss`,
    `_normalgloss`, or `_ng` suffix - RGB is the normal map, alpha is gloss),
    **or**
  - A standalone gloss texture (`_gloss`, `_glossiness`, or `_smoothness`
    suffix)

When this path is taken, the specular color and gloss textures are merged
into a single RGBA image (specular color in RGB, gloss in alpha) and saved
with a `_rmao` suffix so the engine's filename-based routing picks it up
correctly.

**Routes to the Kenny PBR converter (Path 2) when it finds:**
- A base color/diffuse texture
- A specular texture, but **no** gloss data (no combined normal+gloss map and
  no standalone gloss map)
- And/or just a bump/normal map with no specular texture at all

In this case the specular texture (if present) is saved with a plain
filename - deliberately *without* the `_rmao` suffix - so it falls through to
the legacy approximation path instead.

**Also still supported: Roughness/Metallic import (`_roughness` +
`_metallic` + `_ao` suffixes).** This predates the switch to Specular/Gloss
as the default and is kept for anyone deliberately working with
metallic-authored source textures (e.g. glTF/UE-convention assets), but its
output will only render correctly as true metallic-workflow PBR if the
lighting shaders are reverted to interpret the `USE_PBR` stage that way again
- as shipped in this fork, that data will be misinterpreted as specular/gloss
data instead. Prefer the specular+gloss naming conventions above for new
content.

**Recognized normal map suffixes** (checked before falling back to plain
normal detection): `_normal_gloss`, `_nrm_gloss`, `_normalgloss`, `_ng`
(combined normal+gloss), then `_normal`, `_nor`, `_nrm`, `_nrml`, `_norm`,
`_nor_dx`, `_nor_gl`, `_normal_directx`, `_normal_opengl` (plain normal map,
DirectX-convention names get their green channel inverted automatically).

---

## Material keyword reference

| Keyword | Stage | Notes |
|---------|-------|-------|
| `diffusemap` / `basecolormap` | Diffuse/albedo | Interchangeable aliases |
| `bumpmap` / `normalmap` | Normal map | Interchangeable aliases. Use `invertGreen( normalmap.png )` to flip the Y axis for DirectX-convention normal maps |
| `specularmap` / `rmaomap` | Specular | Interchangeable aliases - same internal stage. Which BRDF path this resolves to at render time depends on the filename (`_rmao`/`_rmaod` suffix = Specular/Gloss PBR, anything else = Kenny PBR legacy approximation) |
| `qer_editorimage` | Editor-only preview | Not used at runtime, just shown in DarkRadiant/editor viewports |

## Ambient occlusion

The Specular/Gloss PBR path (Path 1) does not currently read a dedicated AO
channel - the specular map's 4 channels are already fully used by specular
color (RGB) and gloss (alpha). Screen-space ambient occlusion still applies
normally on top regardless of which path a material uses.
