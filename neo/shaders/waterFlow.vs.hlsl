/*
===========================================================================

KJ: BO3-style flowmap water vertex shader.

Simple pass-through in the spirit of heathaze.vs.hlsl - the actual flowmap
distortion work happens in the fragment shader (waterFlow.ps.hlsl), since it
depends on a texture lookup (the flowmap) that isn't available at the vertex
stage in a useful way for per-pixel water ripple detail.

===========================================================================
*/

#include "global_inc.hlsl"
#include "renderParmSet8.inc.hlsl"

// *INDENT-OFF*
#if USE_GPU_SKINNING
StructuredBuffer<float4> matrices : register(t11);
#endif

struct VS_IN
{
	float4 position	: POSITION;
	float2 texcoord	: TEXCOORD0;
	float4 normal	: NORMAL;
	float4 tangent	: TANGENT;
	float4 color	: COLOR0;
	float4 color2	: COLOR1;
};

struct VS_OUT
{
	float4 position		: SV_Position;
	float4 texcoord0	: TEXCOORD0_centroid;	// base UV (xy), time (z), unused (w)
	float4 texcoord1	: TEXCOORD1_centroid;	// distortion magnitude (x), unused (yzw)
};
// *INDENT-ON*

void main( VS_IN vertex, out VS_OUT result )
{

#include "skinning.inc.hlsl"

	// base texture coordinates, no scroll here - the flowmap in the fragment
	// shader drives all the UV animation for this material
	result.texcoord0 = float4( vertex.texcoord.xy, pc.rpUser0.x, 0 );

	// distortion magnitude for perturbing the _currentRender screen lookup,
	// same falloff-with-distance trick as heathaze.vs.hlsl so the effect
	// doesn't get wildly exaggerated right up against the near plane
	float4 vec = float4( 0, 1, 0, 1 );
	vec.z = dot4( modelPosition, pc.rpModelViewMatrixZ );

	const float magicProjectionAdjust = 0.43f;
	float x = vec.y * pc.rpProjectionMatrixY.y * magicProjectionAdjust;
	float w = dot4( vec, pc.rpProjectionMatrixW );

	w = max( w, 1.0 );
	x /= w;
	x = min( x, 0.02 );

	result.texcoord1 = float4( x * pc.rpUser1.x, 0, 0, 0 );
}
