/*
===========================================================================

RB: BO3-style flowmap water fragment shader.

Technique: the flowmap (RG channels, remapped from [0,1] storage to a
[-1,1] direction vector) drives two time-offset UV distortions of the
normal map. Sampling at two phases 0.5 apart and cross-fading between them
hides the seam that would otherwise appear every time a single continuously
scrolling UV wraps back around - this is the standard "flow map" trick
(see Valve's 2010 GDC water talk, or any modern flowing-water shader).

Reflection/refraction uses the same _currentRender screen-space distortion
technique as heathaze.ps.hlsl/glass materials elsewhere in this codebase,
perturbed by the flowed normal map and blended with a simple Fresnel term.

Texture budget note: the custom-material shader binding layout
(BINDING_LAYOUT_POST_PROCESS_INGAME, see RenderProgs.cpp) only provisions 3
texture slots, so unlike the standard PBR interaction shaders this does NOT
sample a full per-pixel specular color texture - water's specular response
is realistically a near-constant pale tint anyway, not something that varies
meaningfully per-pixel the way a general material's F0 might, so it's a
material-level color (rpUser0.yzw) instead of a texture. Gloss is packed
into the normal map's alpha channel (same "normal+gloss" combined convention
docs/PBR_MATERIALS.md describes for the makeMaterials importer), rather than
needing its own texture slot too. Water is a single self-contained shader
like glass/heatHaze here, not lit through the full per-light dynamic
lighting pipeline like opaque geometry.

===========================================================================
*/

#include "global_inc.hlsl"
#include "renderParmSet8.inc.hlsl"

// *INDENT-OFF*
Texture2D t_CurrentRender		: register( t0 VK_DESCRIPTOR_SET( 0 ) );
Texture2D t_NormalMap			: register( t1 VK_DESCRIPTOR_SET( 0 ) );	// RGB = normal, A = gloss
Texture2D t_FlowMap			: register( t2 VK_DESCRIPTOR_SET( 0 ) );	// RG = flow direction

SamplerState LinearSampler		: register( s0 VK_DESCRIPTOR_SET( 1 ) );

struct PS_IN {
	float4 position		: SV_Position;
	float4 texcoord0	: TEXCOORD0_centroid;	// base UV (xy), time (z)
	float4 texcoord1	: TEXCOORD1_centroid;	// screen distortion magnitude (x)
};

struct PS_OUT {
	float4 color : SV_Target0;
};
// *INDENT-ON*

// how far apart the two flow phases are, in the same units as time (seconds)
// - half the cycle length, keep matched to how fast rpUser1.y (flow speed)
// scrolls the UVs in one full loop
static const float FLOW_PHASE_OFFSET = 0.5;

void main( PS_IN fragment, out PS_OUT result )
{
	float2 baseUV = fragment.texcoord0.xy;
	float time = fragment.texcoord0.z;

	// rpUser0.yzw = specular tint color (water's F0 - a near-constant pale
	// color rather than a per-pixel texture, see file header)
	float3 specColor = pc.rpUser0.yzw;

	// rpUser1.y = flow speed/strength, rpUser1.z = texture tiling scale for
	// the normal/flowmap samples
	float flowStrength = pc.rpUser1.y;
	float tiling = max( pc.rpUser1.z, 0.0001 );

	// flowmap sample is NOT phase-distorted itself - it's the thing driving
	// the distortion of everything else, so it stays on the base UV
	float2 flowSample = t_FlowMap.Sample( LinearSampler, baseUV ).rg;
	float2 flowDir = ( flowSample * 2.0 ) - 1.0;

	// two time phases, offset by half a cycle, each wrapping with frac() so
	// they sawtooth-reset independently
	float phase1 = frac( time + 0.0 );
	float phase2 = frac( time + FLOW_PHASE_OFFSET );

	float2 uv1 = ( baseUV * tiling ) - ( flowDir * phase1 * flowStrength );
	float2 uv2 = ( baseUV * tiling ) - ( flowDir * phase2 * flowStrength );

	// triangle-wave crossfade weight that peaks at 0.5 blend when either
	// phase is mid-cycle and goes to a hard cut only right at the reset,
	// which is exactly when that phase's sample is about to pop anyway
	float blendWeight = abs( ( phase1 * 2.0 ) - 1.0 );

	// sample normal+gloss map at both phases and blend
	float4 normalGloss1 = t_NormalMap.Sample( LinearSampler, uv1 );
	float4 normalGloss2 = t_NormalMap.Sample( LinearSampler, uv2 );
	float4 normalGloss = lerp( normalGloss1, normalGloss2, blendWeight );

	float2 localNormal = ( normalGloss.xy * 2.0 ) - 1.0;
	float roughness = max( 0.05, 1.0 - normalGloss.a );

	// perturb the screen-space lookup by the flowed normal, same technique
	// as heathaze.ps.hlsl
	float2 screenTexCoord = vposToScreenPosTexCoord( fragment.position.xy );
	screenTexCoord += ( localNormal * fragment.texcoord1.x );
	screenTexCoord = saturate( screenTexCoord );

	float3 sceneColor = t_CurrentRender.Sample( LinearSampler, screenTexCoord.xy ).rgb;

	// simple Schlick-ish Fresnel using the perturbed normal's length as a
	// cheap stand-in for view angle: water looks more mirror-like at a
	// glancing view and more see-through looking straight down
	float NdotV = saturate( 1.0 - length( localNormal ) );
	float fresnelBase = saturate( ( specColor.r + specColor.g + specColor.b ) / 3.0 );
	float fresnel = fresnelBase + ( 1.0 - fresnelBase ) * pow( 1.0 - NdotV, 5.0 );

	// tint the refracted scene faintly toward the specular color (murky/
	// tinted water rather than perfectly clear glass) and blend toward a
	// brighter reflective response at grazing angles
	float3 refractionColor = sceneColor * lerp( float3( 1.0, 1.0, 1.0 ), specColor + 0.5, 0.15 );
	float3 reflectionColor = sceneColor + ( specColor * ( 1.0 - roughness ) );

	float3 finalColor = lerp( refractionColor, reflectionColor, fresnel );

	result.color = float4( finalColor, 1.0 );
}
