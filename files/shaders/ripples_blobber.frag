#version 120

uniform sampler2D imageIn;

#define MAX_POSITIONS 100
uniform vec3 positions[MAX_POSITIONS];
uniform int positionCount;

uniform float osg_SimulationTime;
uniform vec2 offset;

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

void main()
{
    const float RIPPLE_MAP_SIZE = 1024.0;
    vec2 uv = (gl_FragCoord.xy + offset) / RIPPLE_MAP_SIZE;

    vec4 color = texture2D(imageIn, uv);
    float wavesizeMultiplier = getTemporalWaveSizeMultiplier(osg_SimulationTime);
    for (int i = 0; i < positionCount; ++i)
    {
        float wavesize = wavesizeMultiplier * positions[i].z;
        float displace = clamp(0.2 * abs(length((positions[i].xy + offset) - gl_FragCoord.xy) / wavesize - 1.0) + 0.8, 0.0, 1.0);
        color.rg = mix(vec2(-1.0), color.rg, displace);
    }

    gl_FragColor = color;
}
