# PBR Material Workflow

This fork replaces RBDOOM-3-BFG's original **Roughness/Metallic** (Unreal-style)
PBR workflow with a **Specular/Gloss** (CryEngine-style) workflow as the
default interpretation of PBR materials. Classic Doom 3 Blinn-Phong content
still works, and is automatically improved by an approximation pass ("Kenny
PBR") rather than requiring reauthoring.

If you're used to the upstream RBDOOM-3-BFG PBR docs: everywhere they say
"Roughness/Metallic", read "Specular/Gloss" instead.

## Filename suffixes for texture authors

This is the reference table if you're hand-authoring or exporting textures
for this fork:

| Suffix | Stage | Contents |
|--------|-------|----------|
| *(none)* | Diffuse / albedo | No suffix needed - plain filename is fine. |
| *(none, or any normal-map name)* | Normal | Just a regular tangent-space normal map. No suffix requirement - see the recognized name list below. |
| `_ao` / `_ambient` / `_occlusion` | Ambient occlusion | Standalone grayscale mask, sampled live at render time as its own material stage. Keep it as a separate file - there's no combined format for this. |
| `_spec` / `_specular` | Specular color only | RGB = specular color (F0). Combine with a gloss map (below) to get a full `_sgmap`-equivalent result via `makeMaterials`, or supply gloss packed into a normal map's alpha channel (a "normal+gloss" texture, see below). |
| `_gloss` / `_glossiness` / `_smoothness` | Gloss only | Standalone grayscale gloss/smoothness map, paired with a separate specular color map. |
| `_sgmap` | Specular + Gloss combined | RGB = specular color (F0), alpha = gloss. This is what `makeMaterials` generates when it finds separate specular + gloss source textures, and it's what `idMaterial::ParseStage` (`neo/renderer/Material.cpp`) looks for to route a material through the Specular/Gloss PBR path instead of the Kenny PBR legacy approximation. |

The legacy `_rmao`/`_rmaod` suffixes are still recognized wherever `_sgmap`
is (both by the material parser and by the image loader's `_s` → PBR-variant
lookup in `neo/renderer/Image_files.cpp`), so content generated before this
fork settled on `_sgmap` keeps working. New content, and anything
`makeMaterials` generates from now on, uses `_sgmap`.

There is no combined Normal+AO texture format in this fork. Keep normal maps
and AO maps as separate files - AO is sampled live as its own material
stage at render time (see below), so there's no filesize-saving reason to
merge it with anything else, and no channel to spare in the normal map to
merge it into anyway (see the "why AO is a separate texture" note further
down if you're curious).

## The three specular rendering paths

Every material's specular stage is interpreted one of three ways at render
time, decided automatically per-material with no extra `.mtr` keywords
needed:

| Path | When it's used | What it does |
|------|-----------------|--------------|
| **Specular/Gloss PBR** (new default) | Specular texture's filename contains `_sgmap` (or, for old content, `_rmao`/`_rmaod`) | Reads the texture directly as PBR data - no guessing. RGB = specular color (F0), alpha = gloss. |
| **Kenny PBR converter** (legacy approximation) | Any other specular texture (classic Doom 3 spec maps, no special filename) | Approximates plausible PBR roughness/specular values *from* a traditional Blinn-Phong specular texture. Makes old content look noticeably better under modern lighting without touching the source assets. |
| **No specular stage at all** | Material only has `diffusemap`/`bumpmap` | Falls back to the engine's default specular texture, which resolves through the Kenny PBR path above. |

The routing check lives in `idMaterial::ParseStage` (`neo/renderer/Material.cpp`)
and looks for the `_sgmap`/`_rmao`/`_rmaod` substrings in the specular image's
filename. The actual shading math for all three paths lives in
`neo/shaders/BRDF.inc.hlsl` and is consumed by the `USE_PBR` / `KENNY_PBR`
branches in each lighting shader (`interaction.ps.hlsl`,
`interactionSM.ps.hlsl`, `interactionAmbient.ps.hlsl`,
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
  specularmap       models/mapobjects/pbr/materialorb/substance/metal04_sgmap.png
  aomap             models/mapobjects/pbr/materialorb/substance/metal04_ao.png
}
```

The `specularmap` (or `rmaomap` - they're interchangeable aliases for the
same material stage) texture file must have `_sgmap` (or the legacy
`_rmao`/`_rmaod`) somewhere in its filename for the engine to route it
through this path. `aomap` (or `occlusionmap`) is optional and independent
of which specular path a material uses - see the Ambient Occlusion section
below.

---

## Path 2: Kenny PBR converter (legacy Blinn-Phong content)

This is what automatically happens to *any* material using a classic Doom 3
specular texture (the traditional `_s.tga`-style specular/gloss map, or any
specular filename that doesn't contain `_sgmap`/`_rmao`/`_rmaod`). You don't
need to do anything to opt into this - it's the fallback, and it's a
meaningful visual upgrade over flat Blinn-Phong even though the source
texture was never designed with PBR in mind.

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

## Ambient occlusion

AO is its own material stage - `aomap` (alias `occlusionmap`) - independent
of the diffuse/normal/specular stages and independent of which of the two
specular paths above a material uses. It's declared and sampled just like
`bumpmap`/`specularmap`/`diffusemap`:

```
aomap    textures/base_wall/snpanel2_ao.tga
```

At render time this is genuinely sampled per-pixel and multiplied into the
lit result - it's not baked into another texture and it's not a leftover
unused channel. See `idMaterial::GetAOStage()` in `Material.cpp`, the
`aoImage` field on `drawInteraction_t` in `RenderCommon.h`, and the `t_AO`
texture sample near the end of each lighting shader
(`interaction.ps.hlsl`, `interactionSM.ps.hlsl`, `interactionAmbient.ps.hlsl`,
`ambient_lighting_IBL.ps.hlsl`, `ambient_lightgrid_IBL.ps.hlsl`) for the
actual implementation. Materials with no `aomap` stage sample a neutral
white (no occlusion) fallback texture, so leaving it out is always safe.

**One honesty note on physical accuracy:** in the ambient/IBL shaders
(`ambient_lighting_IBL.ps.hlsl`, `ambient_lightgrid_IBL.ps.hlsl`), AO is
combined with SSAO and applied only to the indirect diffuse/specular
terms - this is the textbook-correct place for AO to apply. In the direct
per-light shaders (`interaction.ps.hlsl`, `interactionSM.ps.hlsl`,
`interactionAmbient.ps.hlsl`), it's multiplied into the *entire* lit
result, direct light included. Real-time direct lighting doesn't have a
separate "indirect only" term to attenuate here, so this is a deliberate,
visible-everywhere tradeoff rather than an oversight - if you only want AO
affecting bounced/ambient light, that already happens correctly in the
IBL/ambient passes; the direct-light darkening is the extra, more
aggressive effect on top of that.

**Why AO is a standalone texture rather than packed into another map:**
every existing texture stage in this engine already has its channels fully
committed to something else. Diffuse is packed as YCoCg for better DXT5
compression (all 4 channels used). Normal maps use a DXT5nm trick (X in
alpha, Y in green, R/B zeroed by the compressor - no spare channel).
Specular is fully used by the `_sgmap` layout (F0 color + gloss). Rather
than sacrifice one of those encodings' precision to free up a channel, AO
got its own real texture bind point (`t12` in the material binding set,
alongside normal/specular/basecolor at `t0`-`t2`) - see
`defaultMaterialLayoutDesc` in `RenderProgs.cpp` and the corresponding
`GetCurrentBindingLayout()` branches in `RenderBackend_NVRHI.cpp`. It's
`t12` and not, say, `t3` because `VK_DESCRIPTOR_SET()` (which is what lets
`t3` in the material set and `t3` in the light set coexist) only means
anything for the SPIRV build - it's a no-op for DXIL, where every `t`
register in a shader shares one flat namespace regardless of which
conceptual "set" it's declared under. `t11` is already taken too - it's
the skinned-mesh joint/bone matrix buffer (`StructuredBuffer_SRV`, which
shares the same `t`-register namespace as `Texture_SRV`) declared in the
skinning-specific `uniformsLayoutDesc`. `t12` is the first register free
across every layout combined into any of the interaction/ambient
pipelines, skinned or not.

---

## makeMaterials: automatic material generation from loose textures

The in-engine console command `makeMaterials <folder>` scans a folder of
loose texture files and generates a `.mtr` declaration automatically, based
on filename suffixes. It decides which of the two specular paths above to
route a material through based on what source textures it actually finds:

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
with a `_sgmap` suffix so the engine's filename-based routing picks it up
correctly.

**Routes to the Kenny PBR converter (Path 2) when it finds:**
- A base color/diffuse texture
- A specular texture, but **no** gloss data (no combined normal+gloss map and
  no standalone gloss map)
- And/or just a bump/normal map with no specular texture at all

In this case the specular texture (if present) is saved with a plain
filename - deliberately *without* the `_sgmap` suffix - so it falls through
to the legacy approximation path instead.

**A standalone `_ao`/`_ambient`/`_occlusion` texture, if found, is emitted
as a real `aomap` stage** referencing the source file directly - no merging,
renaming, or re-encoding. This is what actually applies ambient occlusion
at render time (see the Ambient Occlusion section above).

**Metallic-workflow import (`_roughness` + `_metallic` suffixes, and UE4's
packed AO/Roughness/Metal `_specular` texture in `-ue4` mode) is converted
into a real `_sgmap` texture rather than kept in its own layout** - the
`USE_PBR` shader path only understands specular/gloss data, so metallic
source content is transformed at import time instead of producing a texture
the engine would misinterpret:

- **Specular (RGB)** is written as flat mid-grey (`128,128,128`). A
  metallic/roughness texture set doesn't actually carry a specular color to
  convert - metalness alone doesn't tell you F0 - so a neutral placeholder is
  the honest conversion rather than a guess. Author a real specular color
  map (Path 1) if you want an accurate result.
- **Gloss (alpha)** is derived by inverting the roughness map
  (`gloss = 255 - roughness`), since roughness *is* real, usable data.
- The metallic map itself isn't used numerically in the conversion (there's
  no metallic channel in the specular/gloss layout to put it in) - its
  presence is just the signal that this is a metallic-workflow texture set.
- The AO channel packed inside a UE4-style ORM texture specifically isn't
  picked up by this conversion (it's a different file than the standalone
  `_ao`/`_ambient`/`_occlusion` textures `makeMaterials` looks for). Author a
  separate standalone AO map alongside your metallic source textures if you
  want it carried over.

**Recognized normal map suffixes** (checked before falling back to plain
normal detection): `_normal_gloss`, `_nrm_gloss`, `_normalgloss`, `_ng`
(combined normal+gloss), then `_normal`, `_nor`, `_nrm`, `_nrml`, `_norm`,
`_nor_dx`, `_nor_gl`, `_normal_directx`, `_normal_opengl` (plain normal map,
DirectX-convention names get their green channel inverted automatically).

---

## Material keyword reference

| Keyword | Stage | Notes |
|---------|-------|-------|
| `diffusemap` / `basecolormap` | Diffuse/albedo | Interchangeable aliases. No filename suffix needed. |
| `bumpmap` / `normalmap` | Normal map | Interchangeable aliases. Use `invertGreen( normalmap.png )` to flip the Y axis for DirectX-convention normal maps. No filename suffix needed - AO is not packed in here (see above). |
| `specularmap` / `rmaomap` | Specular | Interchangeable aliases - same internal stage. Which BRDF path this resolves to at render time depends on the filename (`_sgmap`/`_rmao`/`_rmaod` suffix = Specular/Gloss PBR, anything else = Kenny PBR legacy approximation). |
| `aomap` / `occlusionmap` | Ambient occlusion | Interchangeable aliases - a genuine standalone stage, sampled live at render time (see above). No filename suffix required to be recognized by the engine (the `_ao`/`_ambient`/`_occlusion` suffixes are just what `makeMaterials` looks for when auto-generating materials from loose files). |
| `qer_editorimage` | Editor-only preview | Not used at runtime, just shown in DarkRadiant/editor viewports |
