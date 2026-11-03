// ArenaMP HDR colour pipeline.
// Lighting remains in the existing material shaders, while these runtime
// uniforms make the final HDR look adjustable without restarting the game.

uniform int hdrTonemapper; // 0 ACES, 1 Reinhard, 2 Filmic, 3 Neutral, 4 Cinematic
uniform float hdrExposure;
uniform float hdrInteriorExposure;
uniform float hdrNightExposure;
uniform float hdrGamma;
uniform float hdrBrightness;
uniform float hdrSaturation;
uniform bool hdrIsInterior;
uniform float hdrNightFactor;

vec3 acesTonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 reinhardTonemap(vec3 x)
{
    return x / (vec3(1.0) + x);
}

vec3 filmicTonemap(vec3 x)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    vec3 mapped = ((x * (A * x + C * B) + D * E)
        / (x * (A * x + B) + D * F)) - E / F;
    const float whitePoint = 11.2;
    const float white = ((whitePoint * (A * whitePoint + C * B) + D * E)
        / (whitePoint * (A * whitePoint + B) + D * F)) - E / F;
    return clamp(mapped / max(white, 0.0001), 0.0, 1.0);
}

vec3 neutralTonemap(vec3 x)
{
    vec3 mapped = x * (x + 0.0245786) - 0.000090537;
    vec3 divisor = x * (0.983729 * x + 0.4329510) + 0.238081;
    return clamp(mapped / max(divisor, vec3(0.0001)), 0.0, 1.0);
}

// Luxora-inspired cinematic curve: gentle shoulder, preserved mid-tones and
// a very small warm/cool separation. Kept inside ArenaMP's existing HDR
// pipeline so exposure/gamma remain authoritative.
vec3 cinematicTonemap(vec3 x)
{
    x *= 0.72;
    vec3 a = x * (x + 0.08) + 0.004;
    vec3 b = x * (x * 0.90 + 0.52) + 0.065;
    vec3 r = a / max(b, vec3(0.0001));
    float lum = dot(r, vec3(0.2126, 0.7152, 0.0722));
    float shadowMask = 1.0 - smoothstep(0.0, 0.30, lum);
    r.r = pow(max(r.r, 0.0), mix(0.99, 1.01, shadowMask));
    r.b = pow(max(r.b, 0.0), mix(1.01, 0.98, shadowMask));
    return clamp(r, 0.0, 1.0);
}

vec3 preLight(vec3 x)
{
#if @hdrLighting
    return pow(max(x, vec3(0.0)), vec3(2.2));
#else
    return x;
#endif
}

vec3 toneMap(vec3 x)
{
#if @hdrLighting
    float environmentExposure = hdrIsInterior
        ? hdrInteriorExposure
        : hdrNightExposure * clamp(hdrNightFactor, 0.0, 1.0);
    x *= max(hdrExposure + environmentExposure, 0.0);

    vec3 col;
    if (hdrTonemapper == 1)
        col = reinhardTonemap(x);
    else if (hdrTonemapper == 2)
        col = filmicTonemap(x);
    else if (hdrTonemapper == 3)
        col = neutralTonemap(x);
    else if (hdrTonemapper == 4)
        col = cinematicTonemap(x);
    else
        col = acesTonemap(x);

    col *= max(hdrBrightness, 0.0);
    float luminance = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(luminance), col, max(hdrSaturation, 0.0));
    return pow(clamp(col, 0.0, 1.0), vec3(1.0 / max(hdrGamma, 0.1)));
#else
    return x;
#endif
}
