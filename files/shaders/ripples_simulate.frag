#version 120

uniform sampler2D imageIn;

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
    vec2 uv = gl_FragCoord.xy / RIPPLE_MAP_SIZE;

    float pixelSize = 1.0 / RIPPLE_MAP_SIZE;

    float oneOffset = pixelSize;
    float oneAndHalfOffset = 1.5 * pixelSize;

    vec4 n = vec4(
        texture2D(imageIn, uv + vec2(oneOffset, 0.0)).r,
        texture2D(imageIn, uv + vec2(-oneOffset, 0.0)).r,
        texture2D(imageIn, uv + vec2(0.0, oneOffset)).r,
        texture2D(imageIn, uv + vec2(0.0, -oneOffset)).r
    );

    vec4 n2 = vec4(
        texture2D(imageIn, uv + vec2(oneAndHalfOffset, 0.0)).r,
        texture2D(imageIn, uv + vec2(-oneAndHalfOffset, 0.0)).r,
        texture2D(imageIn, uv + vec2(0.0, oneAndHalfOffset)).r,
        texture2D(imageIn, uv + vec2(0.0, -oneAndHalfOffset)).r
    );

    vec4 color = texture2D(imageIn, uv);

    gl_FragColor = applySprings(color, n, n2);
}
