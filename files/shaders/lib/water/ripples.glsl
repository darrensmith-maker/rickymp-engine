#ifndef LIB_WATER_RIPPLES
#define LIB_WATER_RIPPLES

float getTemporalWaveSizeMultiplier(in float time)
{
    return 1.0 + 0.055 * sin(16.0 * time) + 0.065 * sin(12.87645 * time);
}

vec4 applySprings(in vec4 samplerData, in vec4 n, in vec4 n2)
{
    vec4 storage = vec4(0.0, samplerData.r, 0.0, 0.0);

    const float a = 0.28;
    const float udamp = 0.04;
    const float vdamp = 0.04;

    float nsum = n.x + n.y + n.z + n.w;
    storage.r = a * nsum + ((2.0 - udamp - vdamp) - 4.0 * a) * samplerData.r - (1.0 - vdamp) * samplerData.g;
    storage.ba = 2.0 * (n.xz - n.yw) + 0.5 * (n2.xz - n2.yw);

    return storage;
}

#endif
