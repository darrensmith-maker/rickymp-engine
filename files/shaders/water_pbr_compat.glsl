#ifndef ARENAMW_WATER_PBR_COMPAT
#define ARENAMW_WATER_PBR_COMPAT

// OpenMW 0.51 water shader compatibility bridge for ArenaMP/OpenMW 0.47.
// The PBR water only queries the sun (light 0), so keep the 0.47 FFP light
// interface instead of pulling the complete 0.51 light-buffer pipeline.
#define lcalcPosition(lightIndex) (gl_LightSource[lightIndex].position.xyz)
#define lcalcDiffuse(lightIndex)  (gl_LightSource[lightIndex].diffuse.xyz)
#define lcalcSpecular(lightIndex) (gl_LightSource[lightIndex].specular)

uniform sampler2D reflectionMap;
uniform sampler2D refractionMap;
uniform sampler2D refractionDepthMap;

vec4 sampleReflectionMap(vec2 uv)
{
    return texture2D(reflectionMap, clamp(uv, vec2(0.001), vec2(0.999)));
}

vec4 sampleRefractionMap(vec2 uv)
{
    return texture2D(refractionMap, clamp(uv, vec2(0.001), vec2(0.999)));
}

float sampleRefractionDepthMap(vec2 uv)
{
    return texture2D(refractionDepthMap, clamp(uv, vec2(0.001), vec2(0.999))).x;
}

float fresnel_dielectric(vec3 incoming, vec3 normal, float eta)
{
    float c = abs(dot(incoming, normal));
    float g = eta * eta - 1.0 + c * c;
    if (g <= 0.0)
        return 1.0;

    g = sqrt(g);
    float A = (g - c) / (g + c);
    float B = (c * (g + c) - 1.0) / (c * (g - c) + 1.0);
    return 0.5 * A * A * (1.0 + B * B);
}

float linearizeDepth(float depth, float zNear, float zFar)
{
    float z_n = 2.0 * depth - 1.0;
    return 2.0 * zNear * zFar / (zFar + zNear - z_n * (zFar - zNear));
}

vec4 applyFogAtDist(vec4 color, float euclideanDist, float linearDist, float zFar)
{
#if @radialFog
    float dist = euclideanDist;
#else
    float dist = abs(linearDist);
#endif
    float fogValue = clamp((dist - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
    color.xyz = mix(color.xyz, gl_Fog.color.xyz, fogValue);
    return color;
}

#endif
