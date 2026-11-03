#version 120

varying vec3 cloudRay;

uniform float cloudTime;
uniform float cloudDensity;
uniform float cloudCoverage;
uniform float cloudSpeed;
uniform float cloudQuality;
uniform float cloudWeather;
uniform vec3 cloudSkyColor;
uniform vec3 cloudFogColor;
uniform vec3 cloudSunColor;
uniform vec3 cloudSunDirection;

float hash31(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float noise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash31(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
}

float cloudFbm(vec3 p, float quality)
{
    float value = noise3(p) * 0.58;
    p = p * 2.03 + vec3(7.1, 3.7, 5.4);
    value += noise3(p) * 0.27;
    if (quality > 0.5)
    {
        p = p * 2.01 + vec3(2.3, 9.2, 1.7);
        value += noise3(p) * 0.10;
    }
    if (quality > 1.5)
    {
        p = p * 2.07 + vec3(6.8, 1.4, 8.6);
        value += noise3(p) * 0.05;
    }
    return value;
}

void main()
{
    vec3 ray = normalize(cloudRay);
    float horizonFade = smoothstep(-0.03, 0.12, ray.z);
    float zenithFade = 1.0 - smoothstep(0.82, 1.0, ray.z);
    if (horizonFade <= 0.001 || zenithFade <= 0.001)
        discard;

    float requestedSteps = mix(10.0, 24.0, clamp(cloudQuality * 0.5, 0.0, 1.0));
    float travel = 0.35;
    float stepLength = 0.105;
    vec3 wind = vec3(cloudTime * cloudSpeed * 0.018, cloudTime * cloudSpeed * 0.011, 0.0);
    float accumulated = 0.0;
    vec3 colour = vec3(0.0);
    vec3 sunDir = normalize(cloudSunDirection);

    for (int i = 0; i < 24; ++i)
    {
        if (float(i) >= requestedSteps || accumulated > 0.96)
            break;

        vec3 samplePos = ray * travel;
        samplePos.xy /= max(0.18, ray.z + 0.24);
        samplePos = samplePos * vec3(1.15, 1.15, 2.6) + wind;

        float shape = cloudFbm(samplePos * 1.35, cloudQuality);
        float coverage = mix(0.72, 0.30, clamp(cloudCoverage, 0.0, 1.0));
        coverage -= cloudWeather * 0.13;
        float localDensity = smoothstep(coverage, coverage + 0.24, shape);
        localDensity *= cloudDensity * mix(0.72, 1.35, cloudWeather);
        localDensity *= (1.0 - accumulated) * 0.15;

        float light = 0.55 + 0.45 * max(dot(ray, sunDir), 0.0);
        vec3 base = mix(cloudFogColor, vec3(1.0), 0.48 + 0.18 * light);
        base = mix(base, cloudSunColor, max(dot(ray, sunDir), 0.0) * 0.24);
        colour += base * localDensity;
        accumulated += localDensity;
        travel += stepLength;
    }

    float weatherDim = mix(1.0, 0.72, cloudWeather);
    colour *= weatherDim;
    float alpha = accumulated * horizonFade * zenithFade;
    alpha *= smoothstep(0.0, 0.08, ray.z);
    gl_FragColor = vec4(colour + cloudSkyColor * alpha * 0.035, clamp(alpha, 0.0, 0.92));
}
