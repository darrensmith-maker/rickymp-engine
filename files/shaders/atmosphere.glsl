#ifndef ARENAMP_ATMOSPHERE_GLSL
#define ARENAMP_ATMOSPHERE_GLSL

uniform bool arenaSkyBlending;
uniform vec3 arenaSkyColor;
uniform float arenaSkyBlendStart;

vec3 arenaFogColour(vec3 baseFog, vec3 worldDirection)
{
    if (!arenaSkyBlending)
        return baseFog;

    float directionLength = length(worldDirection);
    if (directionLength < 0.0001)
        return baseFog;
    vec3 direction = worldDirection / directionLength;
    // World-space Z is vertical. Blend only close to the horizon so the
    // zenith keeps the authored sky colour and nearby fog remains unchanged.
    float horizon = 1.0 - clamp(abs(direction.z), 0.0, 1.0);
    float start = clamp(arenaSkyBlendStart, 0.0, 0.98);
    float factor = smoothstep(start, 1.0, horizon);
    return mix(baseFog, arenaSkyColor, factor * 0.82);
}

#endif
