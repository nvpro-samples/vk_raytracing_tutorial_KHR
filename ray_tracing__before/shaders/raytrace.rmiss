#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "raycommon.glsl"
#include "wavefront.glsl"

layout(location = 0) rayPayloadInEXT hitPayload prd;
layout(location = 1) rayPayloadInEXT shadowPayload prdShadow;

layout(push_constant) uniform _PushConstantRay
{
	PushConstantRay pcRay;
};

void main()
{
	// Observation: If changing order the clearColor changes to blue ¿¿??
	prdShadow.isHit = false;
	prd.hitValue = pcRay.clearColor.xyz * 0.8;
}