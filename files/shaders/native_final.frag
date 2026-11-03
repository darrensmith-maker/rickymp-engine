#version 120
uniform sampler2D sceneTexture;
uniform sampler2D weightTexture;
uniform sampler2D bloomTexture;
uniform sampler2D depthTexture;
uniform sampler2D overlayDepthTexture;
uniform vec2 inverseSceneSize;
uniform float smaaEnabled;
uniform float bloomEnabled;
uniform float bloomIntensity;

uniform float atmosphericFogEnabled;
uniform float atmosphericFogStrength;
uniform float godRaysEnabled;
uniform float godRaysStrength;
uniform float sharpeningEnabled;
uniform float sharpeningStrength;
uniform float ditheringEnabled;

uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform float cameraNear;
uniform float cameraFar;
uniform vec3 cameraWorldPosition;
uniform vec3 cameraRight;
uniform vec3 cameraUp;
uniform vec3 cameraForward;
uniform float cameraTanHalfFovY;
uniform float cameraAspect;
uniform float environmentExterior;
uniform float environmentUnderwater;
uniform vec2 sunScreenPosition;
uniform float sunVisible;
uniform vec3 sunColor;
uniform float firstPersonView;
uniform float frameTime;

float linearDepth(float depth)
{
    float n = max(cameraNear, 0.001);
    float f = max(cameraFar, n + 1.0);
    float z = depth * 2.0 - 1.0;
    return (2.0 * n * f) / max(f + n - z * (f - n), 0.0001);
}

float hash12(vec2 p)
{
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec3 reconstructWorldPosition(vec2 uv, float viewDepth)
{
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 ray = normalize(cameraForward
        + cameraRight * (ndc.x * cameraAspect * cameraTanHalfFovY)
        + cameraUp * (ndc.y * cameraTanHalfFovY));
    float forwardCos = max(dot(ray, cameraForward), 0.08);
    return cameraWorldPosition + ray * (viewDepth / forwardCos);
}

float worldDepthAt(vec2 uv)
{
    return texture2D(depthTexture, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

float overlayDepthAt(vec2 uv)
{
    return texture2D(overlayDepthTexture, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

float skyVisibilityAt(vec2 uv)
{
    // World-depth texture is captured before the first-person depth clear, so
    // this test is valid in both camera modes. Values very close to 1 are sky.
    return smoothstep(0.99998, 0.9999995, worldDepthAt(uv));
}

bool firstPersonForeground(vec2 uv)
{
    if (firstPersonView < 0.5)
        return false;

    // After OpenMW clears depth for the first-person bin only hands/weapons
    // write depth. Protect them from world-space fog/light shafts.
    return overlayDepthAt(uv) < 0.9996;
}

vec3 viewRayForUv(vec2 uv)
{
    vec2 ndc = uv * 2.0 - 1.0;
    return normalize(cameraForward
        + cameraRight * (ndc.x * cameraAspect * cameraTanHalfFovY)
        + cameraUp * (ndc.y * cameraTanHalfFovY));
}

vec3 applyAtmosphericFog(vec3 color, vec2 uv, float rawDepth)
{
    if (environmentExterior < 0.5 || environmentUnderwater > 0.5 || firstPersonForeground(uv))
        return color;

    vec3 ray = viewRayForUv(uv);
    float strength = clamp(atmosphericFogStrength, 0.0, 1.0);
    if (strength <= 0.0001)
        return color;

    // Sky/horizon aerial perspective. The previous version returned early for
    // depth=1, so in first person (where depth had been cleared) the effect was
    // effectively invisible. A small horizon term also makes the fog read as
    // atmospheric volume instead of only recolouring distant geometry.
    if (rawDepth >= 0.99997)
    {
        float horizon = pow(clamp(1.0 - abs(ray.z), 0.0, 1.0), 2.2);
        float skyAmount = clamp(horizon * strength * 0.16, 0.0, 0.18);
        return mix(color, max(fogColor, vec3(0.0)), skyAmount);
    }

    float d = linearDepth(rawDepth);
    float start = max(fogStart * 0.24, cameraNear * 3.0);
    float end = max(fogEnd, start + 1.0);
    float distanceFog = smoothstep(start, end, d);
    if (distanceFog <= 0.0001)
        return color;

    vec3 worldPos = reconstructWorldPosition(uv, d);
    float relativeHeight = worldPos.z - cameraWorldPosition.z;

    // Dense near the ground / low valleys, thinner above the eye line.
    float heightWeight = mix(1.32, 0.48, smoothstep(-420.0, 1650.0, relativeHeight));

    // Two very low-frequency world-space noise octaves. The field is anchored
    // to the world, not the screen, so it drifts like fog instead of swimming
    // with camera rotation.
    vec2 noiseCoord = worldPos.xy * 0.00105 + vec2(frameTime * 0.010, -frameTime * 0.0065);
    float n0 = valueNoise(noiseCoord);
    float n1 = valueNoise(noiseCoord * 0.43 + vec2(13.7, -9.2));
    float density = mix(0.68, 1.28, n0 * 0.68 + n1 * 0.32) * heightWeight;

    // Exponential response gives a visibly volumetric build-up while staying
    // bounded and preserving the game's native distance fog underneath.
    float optical = distanceFog * strength * density * 0.72;
    float amount = clamp(1.0 - exp(-optical), 0.0, 0.52);
    return mix(color, max(fogColor, vec3(0.0)), amount);
}

float sunDiscCoverage()
{
    if (sunScreenPosition.x <= 0.0 || sunScreenPosition.x >= 1.0
        || sunScreenPosition.y <= 0.0 || sunScreenPosition.y >= 1.0)
        return 0.0;

    // Estimate how much of the *whole apparent sun disc* is visible instead of
    // treating one depth sample as a global on/off switch. This is deliberately
    // wider than a 3x3 kernel: a thin branch/leaf should only remove the part of
    // the disc that it actually covers, while a wall/rock covering the complete
    // disc still drives coverage to zero.
    vec2 r1 = inverseSceneSize * 4.5;
    vec2 r2 = inverseSceneSize * 10.0;
    float v = 0.0;
    float w = 0.0;

    v += skyVisibilityAt(sunScreenPosition) * 0.08; w += 0.08;

    v += skyVisibilityAt(sunScreenPosition + vec2( r1.x, 0.0)) * 0.07; w += 0.07;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r1.x, 0.0)) * 0.07; w += 0.07;
    v += skyVisibilityAt(sunScreenPosition + vec2(0.0,  r1.y)) * 0.07; w += 0.07;
    v += skyVisibilityAt(sunScreenPosition + vec2(0.0, -r1.y)) * 0.07; w += 0.07;
    v += skyVisibilityAt(sunScreenPosition + vec2( r1.x,  r1.y)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r1.x,  r1.y)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2( r1.x, -r1.y)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r1.x, -r1.y)) * 0.055; w += 0.055;

    v += skyVisibilityAt(sunScreenPosition + vec2( r2.x, 0.0)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r2.x, 0.0)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2(0.0,  r2.y)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2(0.0, -r2.y)) * 0.055; w += 0.055;
    v += skyVisibilityAt(sunScreenPosition + vec2( r2.x,  r2.y)) * 0.035; w += 0.035;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r2.x,  r2.y)) * 0.035; w += 0.035;
    v += skyVisibilityAt(sunScreenPosition + vec2( r2.x, -r2.y)) * 0.035; w += 0.035;
    v += skyVisibilityAt(sunScreenPosition + vec2(-r2.x, -r2.y)) * 0.035; w += 0.035;

    return clamp(v / max(w, 0.0001), 0.0, 1.0);
}

vec3 computeGodRays(vec2 uv)
{
    if (sunVisible < 0.5 || environmentExterior < 0.5 || environmentUnderwater > 0.5
        || firstPersonForeground(uv))
        return vec3(0.0);

    vec2 toSun = sunScreenPosition - uv;
    float radial = length(toSun);
    if (radial > 1.28)
        return vec3(0.0);

    float sunCoverage = sunDiscCoverage();
    // Only a nearly *fully* covered disc may switch shafts off. Partial
    // coverage (branches, leaves, fences) is intentionally soft so it cannot
    // make the whole frame pop darker in one step.
    float fullOcclusionGate = smoothstep(0.015, 0.12, sunCoverage);
    if (fullOcclusionGate <= 0.001)
        return vec3(0.0);

    float shaftSource = fullOcclusionGate
        * mix(0.72, 1.0, smoothstep(0.14, 0.88, sunCoverage));
    float coreSource = fullOcclusionGate * smoothstep(0.05, 0.82, sunCoverage);

    // Positive-only radial light integration. We never subtract/darken the
    // scene, so occluders create bright shafts through their gaps rather than
    // the long dark bands seen in the previous implementation.
    const float sampleCount = 16.0;
    vec2 stepUv = toSun / sampleCount;
    vec2 p = uv + stepUv * 0.35;
    float decay = 1.0;
    float accumulated = 0.0;
    float norm = 0.0;

    for (int i = 0; i < 16; ++i)
    {
        if (p.x >= 0.0 && p.x <= 1.0 && p.y >= 0.0 && p.y <= 1.0)
        {
            float visibility = skyVisibilityAt(p);
            float w = decay * (1.0 - float(i) * 0.018);
            accumulated += visibility * w;
            norm += w;
        }
        p += stepUv;
        decay *= 0.935;
    }

    float integrated = accumulated / max(norm, 0.0001);
    float localSky = skyVisibilityAt(uv);

    // Suppress the broad "whole sky glows" component while keeping shafts on
    // geometry/fog where the ray path opens toward the sun.
    float shaft = clamp(integrated - localSky * 0.42, 0.0, 1.0);
    shaft = pow(shaft, 1.35);

    float sunFalloff = pow(clamp(1.0 - radial / 1.28, 0.0, 1.0), 1.75);
    float coreGlow = pow(clamp(1.0 - radial / 0.22, 0.0, 1.0), 5.0) * 0.10;

    float sunLuma = dot(max(sunColor, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
    if (sunLuma < 0.015)
        return vec3(0.0);

    vec3 warmSun = mix(max(sunColor, vec3(0.0)),
        vec3(1.0, 0.76, 0.48) * min(1.0, sunLuma + 0.20), 0.20);
    // Keep the broad shaft field stable under thin occluders. Only the sun
    // core follows partial disc coverage strongly; the shaft field mostly
    // responds to the local radial samples above. Full occlusion still fades
    // both terms to zero.
    float intensity = (shaft * 0.92 * shaftSource + coreGlow * coreSource) * sunFalloff;
    return warmSun * intensity * max(godRaysStrength, 0.0) * 0.70;
}

vec3 computeSunGlare(vec2 uv)
{
    if (sunVisible < 0.5 || environmentExterior < 0.5 || environmentUnderwater > 0.5)
        return vec3(0.0);

    float coverage = sunDiscCoverage();

    // Preserve the soft behaviour introduced in V5: a twig or a few leaves
    // only reduce the flash proportionally, while a genuinely covered sun
    // disc fades the effect away.  No GPU occlusion query/readback is needed.
    float visibility = smoothstep(0.01, 0.22, coverage);
    if (visibility <= 0.0001)
        return vec3(0.0);

    vec2 d = uv - sunScreenPosition;
    d.x *= max(cameraAspect, 0.01);
    float radius = length(d);

    vec2 centerDelta = sunScreenPosition - vec2(0.5);
    centerDelta.x *= max(cameraAspect, 0.01);
    float facing = clamp(1.0 - length(centerDelta) / 0.92, 0.0, 1.0);
    facing = facing * facing;

    float sunLuma = dot(max(sunColor, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
    if (sunLuma < 0.015)
        return vec3(0.0);

    vec3 warmSun = mix(max(sunColor, vec3(0.0)),
        vec3(1.0, 0.76, 0.48) * min(1.0, sunLuma + 0.20), 0.22);

    // Local flash/halo plus a weak full-frame veiling glare when the player
    // looks toward the sun.  All terms are positive-only.
    float core = exp(-radius * radius * 190.0) * 0.16;
    float halo = exp(-radius * radius * 24.0) * 0.055;
    float wash = facing * 0.032;

    // Thin occluders affect the small core more strongly than the broad wash,
    // avoiding the old whole-screen pop when a branch crosses the sun.
    float softCoverage = mix(0.72, 1.0, smoothstep(0.12, 0.88, coverage));
    float local = (core * smoothstep(0.04, 0.80, coverage) + halo * softCoverage) * visibility;
    float veil = wash * visibility * softCoverage;

    // Do not paint a bright local blob over first-person hands/weapons, but
    // retain the subtle full-screen veiling glare.
    if (firstPersonForeground(uv))
        local *= 0.12;

    return warmSun * (local + veil);
}

vec3 sharpenCAS(vec2 uv, vec3 center)
{
    vec2 dx = vec2(inverseSceneSize.x, 0.0);
    vec2 dy = vec2(0.0, inverseSceneSize.y);
    vec3 b = texture2D(sceneTexture, clamp(uv - dy, vec2(0.0), vec2(1.0))).rgb;
    vec3 d = texture2D(sceneTexture, clamp(uv - dx, vec2(0.0), vec2(1.0))).rgb;
    vec3 f = texture2D(sceneTexture, clamp(uv + dx, vec2(0.0), vec2(1.0))).rgb;
    vec3 h = texture2D(sceneTexture, clamp(uv + dy, vec2(0.0), vec2(1.0))).rgb;

    vec3 mn = min(center, min(min(b, d), min(f, h)));
    vec3 mx = max(center, max(max(b, d), max(f, h)));
    vec3 amp = clamp(min(mn, 2.0 - mx) / max(mx, vec3(0.001)), 0.0, 1.0);
    amp = inversesqrt(max(amp, vec3(0.01)));
    float peak = mix(8.0, 5.0, clamp(sharpeningStrength, 0.0, 1.0));
    vec3 w = -1.0 / (amp * peak);
    vec3 outColor = ((b + d + f + h) * w + center) / max(1.0 + 4.0 * w, vec3(0.05));
    return clamp(outColor, mn * 0.92, mx * 1.08);
}

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseSceneSize;
    float rawDepth = texture2D(depthTexture, uv).r;
    vec3 color = texture2D(sceneTexture, uv).rgb;

    if (smaaEnabled >= 0.5)
    {
        vec4 w = texture2D(weightTexture, uv);
        float hw = clamp(w.x + w.y, 0.0, 0.92);
        float vw = clamp(w.z + w.w, 0.0, 0.92);
        vec3 h = texture2D(sceneTexture, clamp(uv - vec2(inverseSceneSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb * w.x
               + texture2D(sceneTexture, clamp(uv + vec2(inverseSceneSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb * w.y;
        vec3 v = texture2D(sceneTexture, clamp(uv - vec2(0.0, inverseSceneSize.y), vec2(0.0), vec2(1.0))).rgb * w.z
               + texture2D(sceneTexture, clamp(uv + vec2(0.0, inverseSceneSize.y), vec2(0.0), vec2(1.0))).rgb * w.w;
        float total = hw + vw;
        if (total > 0.0001)
        {
            vec3 blended = (h + v) / max(total, 0.0001);
            color = mix(color, blended, clamp(total * 0.68, 0.0, 0.80));
        }
    }

    if (sharpeningEnabled >= 0.5)
        color = mix(color, sharpenCAS(uv, color), clamp(sharpeningStrength, 0.0, 1.0));

    if (bloomEnabled >= 0.5)
        color += max(texture2D(bloomTexture, uv).rgb, vec3(0.0)) * max(bloomIntensity, 0.0);

    if (atmosphericFogEnabled >= 0.5)
        color = applyAtmosphericFog(color, uv, rawDepth);

    // Replacement for the legacy query-driven SunFlash/SunGlare.  It uses
    // the preserved world-depth texture, so occlusion is stable in both
    // first- and third-person views and does not depend on osg::OcclusionQueryNode.
    color += computeSunGlare(uv);

    if (godRaysEnabled >= 0.5)
        color += computeGodRays(uv);

    if (ditheringEnabled >= 0.5)
    {
        // Tiny ordered/noise dither (sub-LSB at 8-bit output) suppresses visible
        // bands in fog, sky gradients and HDR highlights without adding grain.
        float d = hash12(gl_FragCoord.xy + vec2(mod(frameTime * 17.0, 31.0))) - 0.5;
        color += vec3(d / 255.0);
    }

    gl_FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
