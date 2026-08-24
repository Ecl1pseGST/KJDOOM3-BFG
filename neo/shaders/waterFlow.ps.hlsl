/*
===========================================================================

KJ: BO3-style flowmap water fragment shader.

Technique: the flowmap (RG channels, remapped from [0,1] storage to a
[-1,1] direction vector) drives two time-offset UV distortions of the
color and normal maps. Sampling at two phases 0.5 apart and cross-fading
between them hides the seam that would otherwise appear every time a
single continuously scrolling UV wraps back around - this is the standard
"flow map" trick (see Valve's 2010 GDC water talk, or any modern flowing-
water shader).

Reflection/refraction uses the same _currentRender screen-space distortion
technique as heathaze.ps.hlsl/glass materials elsewhere in this codebase,
perturbed by the flowed normal map and blended with a simple Fresnel term.

KJ: matches the BO3 water shader's texture set exactly - colormap,
normalmap, flowmap. Gloss isn't a texture channel here; BO3's water gloss
barely varies per-material, so it's a per-material constant instead
(vertexParm 1.w) rather than needing its own texture or a spare channel
packed into another map. Same reasoning for tint strength/opacity
(vertexParm 2.x) - source colormap textures commonly ship as a family of
intensity variants (a light, barely-there one alongside progressively
darker/more opaque ones), so how strongly the shader should apply that
tint on top depends on which variant is in use, not a fixed constant.

===========================================================================
*/

#include "global_inc.hlsl"
#include "renderParmSet8.inc.hlsl"

// *INDENT-OFF*
Texture2D t_CurrentRender		: register( t0 VK_DESCRIPTOR_SET( 0 ) );
Texture2D t_ColorMap			: register( t1 VK_DESCRIPTOR_SET( 0 ) );	// RGB = color/tint
Texture2D t_NormalMap			: register( t2 VK_DESCRIPTOR_SET( 0 ) );	// RGB = normal (plain, no packed gloss)
Texture2D t_FlowMap			: register( t3 VK_DESCRIPTOR_SET( 0 ) );	// RG = flow direction

SamplerState LinearClampSampler	: register( s0 VK_DESCRIPTOR_SET( 1 ) );	// _currentRender only - a screen-space capture must never wrap
SamplerState LinearWrapSampler		: register( s1 VK_DESCRIPTOR_SET( 1 ) );	// colormap/normalmap/flowmap - needs to tile past [0,1]

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

	// rpUser0.yzw = overall tint color, multiplied with the colormap - lets
	// a mapper push the same colormap toward green/murky or blue/clear
	// without repainting the texture
	float3 tintColor = pc.rpUser0.yzw;

	// rpUser1.y = flow speed/strength, rpUser1.z = texture tiling scale for
	// the color/normal/flowmap samples, rpUser1.w = gloss (fixed per-
	// material constant, not textured - see file header)
	float flowStrength = pc.rpUser1.y;
	float tiling = max( pc.rpUser1.z, 0.0001 );
	float gloss = saturate( pc.rpUser1.w );

	// KJ: rpUser2.x = tint strength (opacity) - how much the color map
	// tints the refracted scene, 0 = fully clear/untinted, 1 = full
	// strength multiplicative tint. BO3's own water reference material
	// this shader is modeled on uses a light 5% tint, which reads as
	// "water" without turning translucent glass into a solid color wash.
	float tintStrength = saturate( pc.rpUser2.x );

	// flowmap sample is NOT phase-distorted itself - it's the thing driving
	// the distortion of everything else, so it stays on the base UV
	float2 flowSample = t_FlowMap.Sample( LinearWrapSampler, baseUV ).rg;
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

	// sample color map at both phases and blend - same flow distortion as
	// the normal map, so surface color/foam detail moves with the current
	float3 colorMap1 = t_ColorMap.Sample( LinearWrapSampler, uv1 ).rgb;
	float3 colorMap2 = t_ColorMap.Sample( LinearWrapSampler, uv2 ).rgb;
	float3 colorMap = lerp( colorMap1, colorMap2, blendWeight ) * tintColor;

	// sample normal map at both phases and blend
	float3 normalMap1 = t_NormalMap.Sample( LinearWrapSampler, uv1 ).rgb;
	float3 normalMap2 = t_NormalMap.Sample( LinearWrapSampler, uv2 ).rgb;
	float3 normalMap = lerp( normalMap1, normalMap2, blendWeight );

	float2 localNormal = ( normalMap.xy * 2.0 ) - 1.0;
	float roughness = max( 0.05, 1.0 - gloss );

	// perturb the screen-space lookup by the flowed normal, same technique
	// as heathaze.ps.hlsl
	float2 screenTexCoord = vposToScreenPosTexCoord( fragment.position.xy );
	screenTexCoord += ( localNormal * fragment.texcoord1.x );
	screenTexCoord = saturate( screenTexCoord );

	float3 sceneColor = t_CurrentRender.Sample( LinearClampSampler, screenTexCoord.xy ).rgb;

	// simple Schlick-ish Fresnel using the perturbed normal's length as a
	// cheap stand-in for view angle: water looks more mirror-like at a
	// glancing view and more see-through looking straight down
	float NdotV = saturate( 1.0 - length( localNormal ) );
	float fresnelBase = saturate( ( colorMap.r + colorMap.g + colorMap.b ) / 3.0 );
	float fresnel = fresnelBase + ( 1.0 - fresnelBase ) * pow( 1.0 - NdotV, 5.0 );

	// tint the refracted scene toward the color map (murky/tinted water
	// rather than perfectly clear glass) and blend toward a brighter
	// reflective response at grazing angles
	// tint the refracted scene toward the color map - a multiplicative
	// tint (like looking through colored glass), so it can only darken or
	// color-shift the scene, never brighten past it
	// KJ: refraction only ever blends toward the tinted result by
	// tintStrength - at the default 5% this reads as barely-tinted glass
	// (mostly the clear scene showing through), not a wash of solid color
	float3 tintedRefraction = sceneColor * saturate( colorMap * 1.5 );
	float3 refractionColor = lerp( sceneColor, tintedRefraction, tintStrength );

	// KJ: reflection blends toward the color map rather than adding it on
	// top of scene brightness - the previous additive version
	// (sceneColor + colorMap * (1-roughness)) stacked up to 80% of the
	// colormap's own brightness directly on top of an already-lit scene,
	// which blew out fast on any colormap with real saturation. This stays
	// bounded between sceneColor and colorMap regardless of gloss.
	float3 reflectionColor = lerp( sceneColor, colorMap, 0.4 * ( 1.0 - roughness ) );

	float3 finalColor = lerp( refractionColor, reflectionColor, fresnel );

	result.color = float4( finalColor, 1.0 );
}
