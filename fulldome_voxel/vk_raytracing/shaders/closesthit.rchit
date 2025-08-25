#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec3 baryCoord;

layout(binding = 3) buffer ColorBuffer {
    vec4 colors[];
};

void main()
{
    uint pointIndex = gl_InstanceCustomIndexEXT;
    hitValue = colors[pointIndex].rgb;
    //hitValue = vec3(1.0f - baryCoord.x - baryCoord.y, baryCoord.x, baryCoord.y);
    //hitValue = vec3(1.0f,1.0f,1.0f);
}
