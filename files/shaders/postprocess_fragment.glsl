#version 120

uniform sampler2D sceneTexture;
uniform vec2 inverseScreenSize;
uniform bool postEnabled;
uniform int postTonemapper; // 0 ACES, 1 Reinhard, 2 Filmic, 3 Neutral
uniform float postExposure;
uniform float postGamma;
uniform float postBrightness;
uniform float postContrast;
uniform float postSaturation;
uniform float postBloomIntensity;
uniform float postBloomThreshold;
uniform float postBloomRadius;

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
    vec3 mapped = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
    float white = ((11.2 * (A * 11.2 + C * B) + D * E) / (11.2 * (A * 11.2 + B) + D * F)) - E / F;
    return clamp(mapped / max(white, 0.0001), 0.0, 1.0);
}

vec3 sceneSample(vec2 uv)
{
    return max(texture2D(sceneTexture, clamp(uv, vec2(0.0), vec2(1.0))).rgb, vec3(0.0));
}

vec3 brightPart(vec3 color)
{
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float knee = max(postBloomThreshold * 0.5, 0.0001);
    float soft = clamp((luminance - postBloomThreshold + knee) / (2.0 * knee), 0.0, 1.0);
    soft = soft * soft * (3.0 - 2.0 * soft);
    float contribution = max(luminance - postBloomThreshold, 0.0) + soft * knee;
    return color * (contribution / max(luminance, 0.0001));
}

vec3 collectBloom(vec2 uv, vec3 center)
{
    vec2 radius = inverseScreenSize * max(postBloomRadius, 0.0);
    vec2 diagonal = radius * 0.70710678;

    // A compact cross + diagonal kernel. It is intentionally single-pass for OpenGL 2.1 compatibility.
    vec3 bloom = brightPart(center) * 2.0;
    bloom += brightPart(sceneSample(uv + vec2(radius.x, 0.0)));
    bloom += brightPart(sceneSample(uv - vec2(radius.x, 0.0)));
    bloom += brightPart(sceneSample(uv + vec2(0.0, radius.y)));
    bloom += brightPart(sceneSample(uv - vec2(0.0, radius.y)));
    bloom += brightPart(sceneSample(uv + diagonal));
    bloom += brightPart(sceneSample(uv - diagonal));
    bloom += brightPart(sceneSample(uv + vec2(diagonal.x, -diagonal.y)));
    bloom += brightPart(sceneSample(uv + vec2(-diagonal.x, diagonal.y)));
    return bloom / 10.0;
}

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseScreenSize;
    vec3 color = sceneSample(uv);

    // Even with post effects disabled, the linear scene still has to be gamma encoded for the monitor.
    if (!postEnabled)
    {
        gl_FragColor = vec4(pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
        return;
    }

    color *= max(postExposure, 0.0);

    if (postBloomIntensity > 0.0001 && postBloomRadius > 0.0001)
        color += collectBloom(uv, color) * max(postBloomIntensity, 0.0);

    vec3 mapped;
    if (postTonemapper == 0)
        mapped = acesTonemap(color);
    else if (postTonemapper == 1)
        mapped = reinhardTonemap(color);
    else if (postTonemapper == 2)
        mapped = filmicTonemap(color);
    else
        mapped = clamp(color, 0.0, 1.0);

    mapped *= max(postBrightness, 0.0);
    mapped = (mapped - vec3(0.5)) * max(postContrast, 0.0) + vec3(0.5);
    float luminance = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
    mapped = mix(vec3(luminance), mapped, max(postSaturation, 0.0));
    mapped = pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / max(postGamma, 0.1)));

    gl_FragColor = vec4(mapped, 1.0);
}
