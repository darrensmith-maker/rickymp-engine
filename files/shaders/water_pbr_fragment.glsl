#version 120

// Set to 0 to disable FBM raymarched waves (requires Enhanced PBR Lighting to function)
#define RAYMARCH_WAVES 1

// Shore foam toggle. 1 = foam on, 0 = foam off.
// Foam also requires the engine's Wobbly Shores setting to be enabled.
// The wobbly shore effect itself is always on (independent of this and of Wobbly Shores).
#define FOAM_ENABLED 1

// Set to 0 to disable distant water reflecting sky color instead of warping reflection UVs
#define WATER_SKY_CONVERGENCE 1


// Inspired by Blender GLSL Water by martinsh ( https://devlog-martinsh.blogspot.de/2012/07/waterundewater-shader-wip.html )
const vec2 BIG_WAVES = vec2(2.31, 2.31);       // strength of big waves
const vec2 MID_WAVES = vec2(0.91, 0.91);       // strength of middle sized waves
const vec2 MID_WAVES_RAIN = vec2(0.98, 0.98);  // strength of middle sized waves in rain
const vec2 SMALL_WAVES = vec2(0.35, 0.35);     // strength of small waves
const vec2 SMALL_WAVES_RAIN = vec2(0.49, 0.49);// strength of small waves in rain

const float VISIBILITY = 1500.0;
const float VISIBILITY_DEPTH = VISIBILITY * 1.5;

const float WAVE_CHOPPINESS = 0.02;                 // wave choppiness
const float WAVE_SCALE = 125.0;                     // overall wave scale

const float BUMP = 0.42;                            // overall water surface bumpiness
const float BUMP_RAIN = 0.56;                       // surface bumpiness in rain
const float REFR_BUMP = 0.015;                      // refraction distortion amount

#if @sunlightScattering
const float SCATTER_AMOUNT = 0.72;                  // amount of sunlight scattering
const vec3  SCATTER_COLOUR = vec3(0.0, 0.65, 0.95); // colour of sunlight scattering
const vec3  SUN_EXT = vec3(0.45, 0.55, 0.68);       // sunlight extinction
#endif

const float SUN_SPEC_FADING_THRESHOLD = 0.16;       // visibility at which sun specularity starts to fade
const float SPEC_HARDNESS = 64.0;                   // specular highlights hardness

// Lua-tunable sky color (shared with lighting_pbr.glsl)
uniform vec3 envSkyAwayColor;
uniform float envSkyStrength;

const float AMBIENT_INTENSITY = 0.60;               // ambient lighting intensity
const float BUMP_SUPPRESS_DEPTH = 40.0;             // at what water depth bumpmap will be suppressed for reflections and refractions (prevents artifacts at shores)
const float REFR_FOG_DISTORT_DISTANCE = 3000.0;     // at what distance refraction fog will be calculated using real water depth instead of distorted depth (prevents splotchy shores)
const float REFL_BUMP = 0.10;                       // reflection distortion amount

#if WATER_SKY_CONVERGENCE
const float SKY_CONVERGENCE_START = 1000.0;         // distance where sky convergence begins
const float SKY_CONVERGENCE_END = 5000.0;           // distance where sky convergence is full
const float SKY_CONVERGENCE_CHOP_SCALE = 0.5;       // how much wave strength drives the sky blend
#endif

const vec2 WIND_DIR = vec2(0.5f, -0.8f);
const float WIND_SPEED = 0.2f;
uniform float osg_SimulationTime;

// Water depth for shore effects (set from refraction when available)
float g_waterDepth = 1000.0;

const float WOBBLY_SHORE_FADE_DISTANCE = 6200.0;    // fade out wobbly shores to mask precision errors, the effect is almost impossible to see at a distance

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -

vec2 normalCoords(vec2 uv, float scale, float speed, float time, float timer1, float timer2, vec3 previousNormal)
{
    time *= 0.25;
    vec2 spatial = uv * (WAVE_SCALE * scale);
    vec2 temporal = WIND_DIR * time * (WIND_SPEED * speed) + vec2(time * timer1, time * timer2);
    vec2 choppiness = -(previousNormal.xy / previousNormal.zz) * WAVE_CHOPPINESS;
    return spatial + temporal + choppiness;
}

vec2 rotateUV(vec2 uv, float angle)
{
    uv -= 0.5;
    float s = sin(angle), c = cos(angle);
    mat2 R = mat2(c, -s, s, c);
    return R * uv + 0.5;
}

uniform sampler2D rippleMap;
uniform vec3 playerPos;
varying vec3 worldPos;
varying vec2 rippleMapUV;
varying vec4 position;
varying float linearDepth;
uniform sampler2D normalMap;
uniform float near;
uniform float far;
uniform float rainIntensity;
uniform bool enableRainRipples;
uniform bool isInteriorWater;
uniform float useRefraction;
uniform float useActorRipples;
// ArenaMP runtime water controls. Defaults preserve the upstream PBR look.
uniform float waterWaveStrength;
uniform float waterSurfaceRoughness;
uniform float waterTransparency;
uniform float waterFoamIntensity;
uniform float waterHighlightIntensity;
varying vec3 screenCoordsPassthrough;

#define WATER 1
#include "shadows_fragment.glsl"

// Minimal PBR helpers used only by this shader. The custom PBR setup uses a
// separate lighting pipeline (lighting_pbr.glsl), so these symbols are provided
// locally to avoid pulling in the shared lighting.glsl that objects/terrain use.
#define PBR_ENABLED 1
#define OMW_VER 51
#define LIGHTING_DEBUG_MODE 0

uniform mat4 osg_ViewMatrixInverse;

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float Schlick(float f0, float f90, float VdotH)
{
    return f0 + (f90 - f0) * Pow5(1.0 - VdotH);
}

float TrowbridgeReitz(float alpha, float NdotH)
{
    float alpha2 = alpha * alpha;
    float denom = (NdotH * NdotH) * (alpha2 - 1.0) + 1.0;
    denom *= denom;
    return alpha2 / denom;
}

#include "water_pbr_compat.glsl"
#include "water_pbr_rain_ripples.glsl"
#include "water_pbr_data.glsl"

#if !defined(HLSL)
    #define float2 vec2
    #define float3 vec3
    #define float4 vec4
    #define float2x2 mat2
    #define float3x3 mat3
    #define saturate(x) clamp(x, 0, 1)
    #define lerp(x, a, b) mix(x, a, b)
#endif // !defined(HLSL)

// ===========================================================================
// Surface detail layer (animated sparkle / breathing / time-of-day tint)
// ===========================================================================

// cheap self-contained value noise
float valueHash2(vec2 p)
{
    p = mod(p, 289.0); // keep coordinates small to preserve float precision at large OpenMW world positions
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise2(vec2 p)
{
    p = mod(p, 289.0); // wrap for stable tiling and mantissa precision
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = valueHash2(i);
    float b = valueHash2(i + vec2(1.0, 0.0));
    float c = valueHash2(i + vec2(0.0, 1.0));
    float d = valueHash2(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Animated glittering of sunlight off ripples.
float surfaceSparkle(vec2 worldXY, vec3 nrm, float time)
{
    vec2 p = worldXY * 0.06;
    float a = valueNoise2(p + vec2(time * 0.13, -time * 0.09));
    float b = valueNoise2(p * 1.7 + vec2(-time * 0.11, time * 0.15));
    float shimmer = a * b;
    float glint = smoothstep(0.62, 0.95, shimmer);
    float micro = valueNoise2(worldXY * 0.22 + time * 0.30);
    micro = smoothstep(0.75, 1.0, micro) * (0.4 + 0.6 * abs(nrm.x + nrm.y));
    return clamp(glint * 0.7 + micro * 0.5, 0.0, 1.0);
}

// Slow large-scale brightness breathing so the whole surface subtly lives.
float surfaceBreathing(vec2 worldXY, float time)
{
    float w = valueNoise2(worldXY * 0.012 + vec2(time * 0.04, time * 0.03));
    return 0.85 + 0.15 * w;
}

// Day/evening/night tint approximated from the sun height.
vec3 dayTint(float sunZ)
{
    float day    = smoothstep(0.05, 0.30, sunZ);            // full daylight
    float sunset = smoothstep(0.30, 0.05, sunZ) * step(0.0, sunZ); // low sun
    vec3 base = mix(vec3(1.1, 1.15, 1.4), vec3(0.95, 0.98, 1.0), day);
    return mix(base, vec3(1.0, 0.8, 0.65), sunset);
}

vec3 GetUnderwaterPos(vec2 fragUV, float d)
{
    float depth =
    #if @reverseZ
        1.0 -
    #endif
        d;
    depth = depth * 2.0 - 1.0;
    vec4 pos = vec4(fragUV * 2.0 - 1.0, depth, 1.0);
    #if PBR_ENABLED
        pos = gl_ProjectionMatrixInverse * pos;
        pos = osg_ViewMatrixInverse * pos;
    #else
        pos = gl_ModelViewProjectionMatrixInverse * pos;
    #endif // PBR_ENABLED
    return pos.xyz / pos.w;
}

float CalculateReflectionAttenuation(vec3 normal)
{
    float cosTheta = clamp(normal.z, 0.0, 1.0);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return clamp(1.0 - sinTheta / max(cosTheta, 0.001), 0.0, 1.0);
}

#if CAUSTICS_ENABLED
// Water's own caustics are disabled (CAUSTICS_ENABLED 0 in water_data.glsl).
// The Voronoi caustics path is not included here; enable it via version control
// if needed.
#error "CAUSTICS_ENABLED is set but the water caustics path is not present here. Restore it from version control or set CAUSTICS_ENABLED 0."
#endif

#if PBR_ENABLED
vec3 GetNoiseWithGradient(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 df = 6.0 * f * (1.0 - f);
    f = f * f * (3.0 - 2.0 * f);
    vec2 np = vec2(0.0, 1.0);
    vec4 ix = mod(i.x + np.xyxy, 271.0);
    vec4 iy = mod(i.y + np.xxyy, 227.0);
    vec4 h = fract(sin(ix * 127.1 + iy * 311.7) * 43758.5453);
    float ba = h.y - h.x;
    float dc = h.w - h.z;
    float ab = mix(h.x, h.y, f.x);
    float cd = mix(h.z, h.w, f.x);
    vec2 grad;
    grad.x = mix(ba, dc, f.y) * df.x;
    grad.y = (cd - ab) * df.y;
    return vec3(mix(ab, cd, f.y), grad);
}

float CalculateWaveHeight(vec2 pos, float time, float distFromCamera, vec4 waveStrength, float targetH, float4 bounds)
{
    vec2 dir1 = -normalize(vec2(0.83f, -0.91f));
    vec2 dir2 = -normalize(vec2(-0.37f, 0.88f));
    vec2 dir3 = -normalize(vec2(-0.59f, -0.71f));
    const mat2 rot1 = mat2(cos(0.6f), -sin(0.6f), sin(0.6f), cos(0.6f));
    const mat2 rot2 = mat2(cos(1.2f), -sin(1.2f), sin(1.2f), cos(1.2f));
    const mat2 rot3 = mat2(cos(2.1f), -sin(2.1f), sin(2.1f), cos(2.1f));
    pos = rot1 * pos * 0.003f;
    pos.x *= 1.17f;
    float h = 0.0f;
    vec3 r = vec3(0.0f);
    float4 fbm = float4(1.164f, 1.0f, 22.5f, 0.0055f * waveStrength.x);
    r = fbm.z * GetNoiseWithGradient(rot1 * (pos * 0.77f * fbm.x + dir1 * time * fbm.y));
    h = r.x * waveStrength.x;
    pos = -r.yz * fbm.w + pos;

    bool doEarlyOut = targetH < 999.0f;
    if (!(doEarlyOut && (h + bounds.x < targetH || h - bounds.x > targetH)))
    {
        fbm *= float4(2.17f, 1.6141f, 0.29f, 1.3f);
        r = fbm.z * GetNoiseWithGradient(rot1 * (pos * 0.92f * fbm.x + dir2 * time * fbm.y));
        h = r.x * waveStrength.x + h;
        pos = -r.yz * fbm.w + pos;

        float lod = smoothstep(10000.0f, 2000.0f, distFromCamera);
        if (lod > 0.0f && !(doEarlyOut && (h + bounds.y < targetH || h - bounds.y > targetH)))
        {
            pos.xy = pos.yx;
            fbm *= float4(2.17f, 1.6141f, 0.46f, 1.3f);
            r = GetNoiseWithGradient(rot2 * (pos * fbm.x + dir3 * time * fbm.y));
            r = lod * fbm.z * mix(r * 0.7f, exp(r * 2.0f - 1.9f), waveStrength.w);
            h = r.x * waveStrength.y + h;
            pos = -r.yz * fbm.w + pos;

            if (!(doEarlyOut && (h + bounds.z < targetH || h - bounds.z > targetH)))
            {
                fbm *= float4(1.618f, 1.346321f, 0.45f, 1.3f);
                r = GetNoiseWithGradient(rot2 * (pos * fbm.x + dir1 * time * fbm.y));
                r = lod * fbm.z * mix(r * 0.7f, exp(r * 2.0f - 1.7f), waveStrength.w);
                h = r.x * waveStrength.y + h;
                pos = -r.yz * fbm.w + pos;

                lod = smoothstep(2000.0f, 700.0f, distFromCamera);
                if (lod > 0.0f && !(doEarlyOut && (h + bounds.w < targetH || h - bounds.w > targetH)))
                {
                    fbm *= float4(1.618f, 1.346321f, 0.45f, 1.3f);
                    r = lod * fbm.z * exp(GetNoiseWithGradient(rot1 * (pos * fbm.x + dir3 * time * fbm.y)) * 2.0f - 1.5f - waveStrength.x * 0.7f);
                    h = r.x * waveStrength.z + h;
                }
            }
        }
    }
    return h * 2.7f - 27.0f;
}

float CalculateWaveHeight(vec2 pos, float time, float distFromCamera, vec4 waveStrength)
{
    return CalculateWaveHeight(pos, time, distFromCamera, waveStrength, 999.9f, float4(0.0f));
}

vec3 RaymarchWater(vec3 eyePos, vec3 direction, float t0, float t1, float time, vec4 waveStrength, bool isUnderwater)
{
    // vertical wave envelope bounds
    float maxWaveHeight =  35.0f * waveStrength.x + worldPos.z;
    float minWaveHeight = -10.5f * waveStrength.x + worldPos.z;
    float tEnter = (maxWaveHeight - eyePos.z) / direction.z;
    float tExit = (minWaveHeight - eyePos.z) / direction.z;
    if (tEnter > tExit)
    {
        float tmp = tEnter;
        tEnter = tExit;
        tExit = tmp;
    }
    tEnter = max(tEnter, t0);
    tExit = min(tExit, t1);
    if (tEnter >= tExit)
        return direction * t1 + eyePos;

    int raySteps = 12; // marching steps through the wave envelope
    float rayStep = (tExit - tEnter) / float(raySteps);

    float invDirZ = 1.0f / max(abs(direction.z), 0.2f);
    float4 bounds = float4(1.37f * waveStrength.z);
    bounds.z = 1.783f * waveStrength.y + bounds.w;
    bounds.y = 3.244f * waveStrength.y + bounds.z;
    bounds.x = 6.525f * waveStrength.x + bounds.y;
    float t = tEnter;
    vec3 p = eyePos;
    float dynamicStep = rayStep;

    for (int i = 0; i < raySteps && t < tExit; i++)
    {
        t += dynamicStep;
        p = direction * t + eyePos;
        float distanceDamping = smoothstep(80.0f, 500.0f, t);
        float cellSize = max(1.0f, t * 0.02f);
        float h = CalculateWaveHeight(p.xy, time, t, waveStrength, (p.z - worldPos.z + 27.0f) / 2.7f, bounds) * distanceDamping;
        float delta = isUnderwater ? h - p.z : p.z - h;

        if (delta <= 0.0f)
            break;
        dynamicStep = clamp(delta * invDirZ, rayStep * 0.25f, rayStep * 3.0f);
    }

    // refine with a short binary search around the last marching interval
    t0 = t - dynamicStep;
    t1 = t;
    for (int i = 0; i < 2; i++)
    {
        t = 0.5f * (t0 + t1);
        p = direction * t + eyePos;
        float distanceDamping = smoothstep(80.0f, 500.0f, t);
        float h = CalculateWaveHeight(p.xy, time, t, waveStrength) * distanceDamping;
        if (isUnderwater ? p.z >= h : p.z <= h)
            t1 = t;
        else
            t0 = t;
    }
    return direction * 0.5 * (t0 + t1) + eyePos;
}
#endif // PBR_ENABLED

void main(void)
{
    vec3 wp = worldPos;
    vec3 cameraPos = (gl_ModelViewMatrixInverse * vec4(0,0,0,1)).xyz;
    bool isUnderwater = cameraPos.z < position.z;
    bool isInterior = isInteriorWater;
    float radialDepth = distance(position.xyz, cameraPos);
    vec3 viewDir = (position.xyz - cameraPos.xyz) / radialDepth;
    vec4 baseWaterColor = WATER_COLOR_SEA;
    float waveStrength = 1.0;
    float multicolorIntensity = 1.0;
    float foamIntensity = 1.0;
    float refractionBrightness = 1.0;
    float sssIntensity = 1.0;
    float shoreSulphurIntensity = 0.0;
    float shoreDistanceModifier = 0.0;

#if MULTIPLE_WATER_TYPES
    {
        UpdateWaterTypes(wp, isInterior, baseWaterColor,
            waveStrength, multicolorIntensity, foamIntensity, refractionBrightness,
            sssIntensity, shoreSulphurIntensity, shoreDistanceModifier);

        if (isUnderwater)
            shoreSulphurIntensity = 0.0;
    }
#endif // MULTIPLE_WATER_TYPES

    // Apply ArenaMP's global controls on top of the per-region water profile.
    // 1.0 keeps the Complete Water Shaders PBR author's original values.
    waveStrength = clamp(waveStrength * waterWaveStrength, 0.0, 2.5);
    foamIntensity = clamp(foamIntensity * waterFoamIntensity, 0.0, 2.0);

    vec4 sunSpec = lcalcSpecular(0);
    // NOTE: upstream mod used a custom waterShadowSoft() (PCF) helper added to a
    // modified shadows_fragment.glsl. Reverted to the stock unshadowedLightRatio()
    // so this ships against the native, unmodified shadow shader.
    float shadow = isInterior ? 1.0 : unshadowedLightRatio(radialDepth);
    vec2 screenCoords = screenCoordsPassthrough.xy / screenCoordsPassthrough.z;
    screenCoords.y = 1.0 - screenCoords.y;
    screenCoords = clamp(screenCoords, vec2(0.001), vec2(0.999));
    float waterTimer = osg_SimulationTime * 0.6;
    float waveTimer = osg_SimulationTime * 0.6;

    vec4 waveStrengthFBM = vec4(waveStrength, sqrt(waveStrength), 0.0f, 0.0f);
    waveStrengthFBM.z = sqrt(waveStrengthFBM.y);
    waveStrengthFBM.w = smoothstep(0.80f, 0.65f, waveStrengthFBM.x);

#if PBR_ENABLED && RAYMARCH_WAVES
    // Only raymarch the near field; distant water shows no usable parallax and
    // is remapped back toward worldPos anyway. The raymarched position is faded
    // back to the flat worldPos across the far transition band to avoid a seam
    // where the FBM normal also vanishes.
    if (radialDepth < 2900.0f && waveStrength > 0.001f)
    {
        wp = RaymarchWater(osg_ViewMatrixInverse[3].xyz, viewDir, 0.0, radialDepth,
                waveTimer, waveStrengthFBM, isUnderwater);
        // near band: blend in parallax from the flat plane (0..500)
        // far band : blend back out to the flat plane (1900..2900)
        float nearBlend = clamp(radialDepth * 0.002f, 0.0, 1.0);
        float farBlend  = smoothstep(2900.0f, 1900.0f, radialDepth);
        wp = mix(worldPos, wp, nearBlend * farBlend);
    }
#endif // PBR_ENABLED && RAYMARCH_WAVES

    vec2 UV = wp.xy / 163840.0f;

#if @waterRefraction
    float depthSample = sampleRefractionDepthMap(screenCoords);
    depthSample = linearizeDepth(depthSample, near, far);
    float surfaceDepth = linearizeDepth(gl_FragCoord.z, near, far);
    float opticalWaterDepth = depthSample - surfaceDepth;
#endif

    vec3 normal0 = vec3(0.0, 0.0, 1.0);
#if PBR_ENABLED
    // The analytic FBM wave normal costs three CalculateWaveHeight() evaluations
    // per fragment. Its contribution is faded smoothly to flat across a transition
    // band (no hard seam) and skipped entirely past the band, so the cost is
    // unchanged on wide ocean views.
    const float FBM_FADE_START = 1900.0; // full detail up to here
    const float FBM_FADE_END   = 2900.0; // fully flat (and skipped) past here
    float fbmLod = smoothstep(FBM_FADE_END, FBM_FADE_START, radialDepth);
    if (fbmLod > 0.0)
    {
        #if RAYMARCH_WAVES
            const float waveSampleDist = 1.0f;
        #else
            float waveSampleDist = max(length(dFdx(wp.xy)), length(dFdy(wp.xy)));
        #endif
        float hC = CalculateWaveHeight(wp.xy, waveTimer, radialDepth, waveStrengthFBM);
        float hX = CalculateWaveHeight(wp.xy + vec2(waveSampleDist, 0.0), waveTimer, radialDepth, waveStrengthFBM);
        float hY = CalculateWaveHeight(wp.xy + vec2(0.0, waveSampleDist), waveTimer, radialDepth, waveStrengthFBM);
        vec3 dX = vec3(waveSampleDist, 0.0, (hX - hC) * fbmLod);
        vec3 dY = vec3(0.0, waveSampleDist, (hY - hC) * fbmLod);
        dX = cross(dX, dY);
        hC = dot(dX, dX);
        if (hC > 0.0)
            normal0 = dX * inversesqrt(hC);
    }
#endif // PBR_ENABLED

    vec2 nc = normalCoords(rotateUV(UV, 0.0), 0.05, 0.04, waterTimer, -0.015, -0.005, vec3(0.0, 0.0, 1.0));
    float lod = log2(1024.0 * max(length(dFdx(nc)), length(dFdy(nc))));
    lod = 1.0 - clamp((lod + 1.0) * 0.05, 0.0, 0.35);
    vec3 normal1 = 2.0 * texture2D(normalMap, nc).rgb - 1.0;
    UV *= 1.73;
    vec3 normal2 = 2.0 * texture2D(normalMap, normalCoords(rotateUV(UV.yx, 1.57), 0.1, 0.08, waterTimer * 1.1, 0.02, 0.015, normal1)).rgb - 1.0;
    UV *= 1.73;
    UV = rotateUV(UV * -4.7, 1.05);
    vec3 normal3 = 2.0 * texture2D(normalMap, normalCoords(rotateUV(UV, 0.0), 0.25, 0.07, -waterTimer * 1.5, -0.04, -0.06, normal2)).rgb - 1.0;
    UV *= 2.1;
    waterTimer *= 7.0;
    vec4 normal4Sample = texture2D(normalMap, normalCoords(rotateUV(UV, 2.4), 0.5, 0.09, -waterTimer * 1.7, 0.03, 0.07, normal3));
    vec3 normal4 = 2.0 * normal4Sample.rgb - 1.0;
    UV *= 2.3;
    waterTimer *= 1.5;
    // normal5 is the smallest, highest-frequency detail layer. Past a moderate
    // distance its sub-pixel ripples are invisible, so skip the dependent fetch
    // beyond ~1200 units and fade its contribution in to avoid a seam.
    vec3 normal5 = vec3(0.0, 0.0, 1.0);
    float n5Lod = smoothstep(1200.0, 700.0, radialDepth);
    if (n5Lod > 0.0)
        normal5 = 2.0 * texture2D(normalMap, normalCoords(rotateUV(UV.yx, 3.8), 1.0, 0.4, -waterTimer * 2.5, -0.02, 0.1, normal4)).rgb - 1.0;
    waterTimer = osg_SimulationTime;
    vec4 rainRipple = vec4(0.0);

#if OMW_VER >= 51
    if (rainIntensity > 0.01)
#else
    if (rainIntensity > 0.01 && enableRainRipples)
#endif
        rainRipple = rainCombined(wp.xy / 350.0, waterTimer) * clamp(rainIntensity, 0.0, 1.0);
    // Distance fade: raindrop rings are a close-up detail. Their sub-pixel rings
    // read as harsh flickering noise toward the horizon, so fade them out with
    // distance the same way the fine wave layers LOD out. Full strength up close,
    // gone by ~3500 units.
    float rainDropFade = 1.0 - smoothstep(1200.0, 3500.0, radialDepth);
    rainRipple *= rainDropFade;
    vec3 rippleAdd = rainRipple.xyz * 5.0;

    // Volumetric raindrop normal. The rings looked flat because only a weak xy
    // tilt reached the surface. Here we build a dedicated steep normal for the
    // impact: strong horizontal slope (the ring wall) with a small z, so once
    // normalized the ring wall stands up and catches light at an angle instead
    // of reading like a decal. rainRipple.xy is the ring-wall slope, .z its
    // upward dome. This is added on top of the wave normal further down.
    vec3 dropNormal = vec3(rainRipple.xy * 14.0, max(rainRipple.z, 0.15));
    float distToCenter = length(rippleMapUV - vec2(0.5));
    // Player and actor wake from the ripple map. blendClose keeps a small
    // dead-zone at the very center; blendFar fades the trail out near the edge of
    // the ripple map. distortionLevel controls how pronounced the wake reads.
    float blendClose = smoothstep(0.001, 0.02, distToCenter);
    // Tighter edge fade: during fast movement the ripple simulation writes rings
    // near the map border that otherwise pop as isolated circles in the distance.
    // Fading earlier (0.26->0.38) removes those before they become visible.
    float blendFar = 1.0 - smoothstep(0.26, 0.38, distToCenter);
    // Distance fade: the ripple map is a fixed grid centred on the player, so far
    // from the camera its texels are large and the wake reads as coarse discrete
    // circles (very visible when swimming fast). Fade the wake out with camera
    // distance so only the coherent near-field trail remains.
    float wakeDistFade = 1.0 - smoothstep(900.0, 2600.0, radialDepth);
    float distortionLevel = 7.0f;
    vec3 playerDistortion = vec3(0.0);
    if (useActorRipples > 0.5)
        playerDistortion = distortionLevel * vec3(texture2D(rippleMap, rippleMapUV).ba * blendFar * blendClose * wakeDistFade, 0.0);
    rippleAdd = playerDistortion + rippleAdd;
    vec2 bigWaves = BIG_WAVES;
    vec2 midWaves = mix(MID_WAVES, MID_WAVES_RAIN, rainIntensity);
    vec2 smallWaves = mix(SMALL_WAVES, SMALL_WAVES_RAIN, rainIntensity);
    float bump = mix(BUMP, BUMP_RAIN, rainIntensity);
    vec3 normal5Faded = mix(vec3(0.0, 0.0, 1.0), normal5, n5Lod);
    vec3 sunWorldDir = normal3 * midWaves.x + normal4 * midWaves.y + normal5Faded * smallWaves.x + normal0 * smallWaves.y;
    vec3 normal = (normal1 * bigWaves.x + normal2 * bigWaves.y + sunWorldDir);
    normal.xy *= lod;
    normal = normalize(vec3(-normal.x * bump, -normal.y * bump, normal.z * 0.3));

    // Apply the user wave multiplier before refraction distortion is sampled,
    // so Wave Strength = 0 really produces a flat/undistorted surface.
    float preDepthWaveStrength = clamp(waveStrength, 0.0, 2.5);
    if (preDepthWaveStrength <= 0.001f)
    {
        normal = vec3(0.0, 0.0, 1.0);
    }
    else if (abs(preDepthWaveStrength - 1.0f) > 0.001f)
    {
        vec2 scaledBigWaves = bigWaves * preDepthWaveStrength;
        float scaledBump = mix(BUMP, BUMP_RAIN, rainIntensity * min(preDepthWaveStrength, 1.0f));
        vec3 scaledNormal = normal1 * scaledBigWaves.x + normal2 * scaledBigWaves.y
            + sunWorldDir * preDepthWaveStrength;
        scaledNormal.xy *= lod;
        normal = normalize(vec3(-scaledNormal.x * scaledBump, -scaledNormal.y * scaledBump, scaledNormal.z * 0.3));
    }

    float waveAttenuation = 1.0;
    vec2 screenCoordsOffset = normal.xy * REFR_BUMP;
    // Raindrop impacts distort the refraction (see-through) sample too, so each
    // drop warps the geometry visible through the water like a real transparent
    // dimple instead of only bending the reflection.
    if (!isUnderwater)
        screenCoordsOffset += dropNormal.xy * REFR_BUMP;

#if @waterRefraction
    screenCoordsOffset *= clamp(opticalWaterDepth / BUMP_SUPPRESS_DEPTH - 0.25, 0.0, 1.0);

    float depthSampleDistorted = sampleRefractionDepthMap(screenCoords - screenCoordsOffset);
    depthSampleDistorted = linearizeDepth(depthSampleDistorted, near, far);
    float waterDepthDistorted = max(depthSampleDistorted - surfaceDepth, 0.0);

    screenCoordsOffset *= clamp(waterDepthDistorted / BUMP_SUPPRESS_DEPTH, 0.0, 1.0);
    if (cameraPos.z > 0.0 && opticalWaterDepth <= VISIBILITY_DEPTH && waterDepthDistorted > VISIBILITY_DEPTH)
        screenCoordsOffset = vec2(0.0);
    depthSampleDistorted = sampleRefractionDepthMap(screenCoords - screenCoordsOffset);
    vec3 underwaterPos = GetUnderwaterPos(screenCoords, depthSampleDistorted);
    float actualWaterDepth = abs(wp.z - underwaterPos.z);
    g_waterDepth = actualWaterDepth;
    depthSampleDistorted = linearizeDepth(depthSampleDistorted, near, far);
    waterDepthDistorted = max(depthSampleDistorted - surfaceDepth, 0.0);
    waterDepthDistorted = mix(waterDepthDistorted, opticalWaterDepth, min(surfaceDepth / REFR_FOG_DISTORT_DISTANCE, 1.0));
    waveAttenuation = smoothstep(-200.0f, 300.0f, actualWaterDepth);
#endif // @waterRefraction

    waveStrength *= waveAttenuation;

    // Underwater, the regular depth-based suppression kills distortion, so
    // objects seen through the surface look flat. Apply a small independent
    // offset so they gently ripple with the waves.
    vec2 underwaterRefrOffset = vec2(0.0);
#if @waterRefraction
    if (isUnderwater)
    {
        // Drive a dedicated offset for the underwater view.
        const float UNDERWATER_REFR_BUMP = 0.05;
        // Big, smooth, low-frequency waves (normal1/normal2) carry the main
        // undulation so the surface visibly sways from below without the
        // high-frequency jitter that pixelates the refraction. The fine layers
        // (normal3/4) are kept only as a faint shimmer.
        vec2 bigSway   = (normal1.xy + normal2.xy) * 0.5;
        vec2 fineRipple = (normal3.xy + normal4.xy) * 0.5;
        // Fold in the player/actor wake and raindrop dimples so from below you
        // see the full relief through the water.
        vec2 surfaceRelief = bigSway * 3.2
                           + fineRipple * 0.35
                           + playerDistortion.xy * 0.9
                           + dropNormal.xy * 0.6;
        underwaterRefrOffset = surfaceRelief * UNDERWATER_REFR_BUMP;
        underwaterRefrOffset = clamp(underwaterRefrOffset, vec2(-0.12), vec2(0.12));
        // Fade out near the Snell horizon (viewDir.z ~ 0) so the transition line
        // is not smeared.
        float horizonGuard = smoothstep(0.0, 0.05, abs(viewDir.z));
        underwaterRefrOffset *= horizonGuard;
    }
#endif

    // Make the global Wave Strength slider affect both raymarched displacement
    // and the normal-map surface. The upstream shader only reduced normals for
    // values below 1.0, which made values above 1.0 visually inconsistent.
    if (waveStrength <= 0.001f)
    {
        normal = vec3(0.0, 0.0, 1.0);
    }
    else if (abs(waveStrength - 1.0f) > 0.001f)
    {
        bigWaves *= waveStrength;
        bump = mix(BUMP, BUMP_RAIN, rainIntensity * min(waveStrength, 1.0f));
        normal = (normal1 * bigWaves.x + normal2 * bigWaves.y + sunWorldDir * waveStrength);
        normal.xy *= lod;
        normal = normalize(vec3(-normal.x * bump, -normal.y * bump, normal.z * 0.3));
    }
    if (isUnderwater)
        normal = -normal;
    if (dot(-viewDir, normal) < 0.0)
        normal = reflect(normal, viewDir);
    float sunFade = smoothstep(-0.3, 1.0, length(gl_LightModel.ambient.rgb));
    float sunFadeSqrt = sqrt(sunFade);
    if (isInterior)
        sunFade = mix(sunFade, 1.0, 0.225);

    sunWorldDir = normalize((gl_ModelViewMatrixInverse * vec4(lcalcPosition(0).xyz, 0.0)).xyz);
    vec3 sunWorldDirUnmodified = sunWorldDir;

#if PBR_ENABLED
    if (isInterior)
        sunWorldDir = vec3(viewDir.x * 0.3, viewDir.y * 0.3, 0.03);
#endif // PBR_ENABLED

    float ior = (!isUnderwater) ? (1.333 / 1.0) : (1.0 / 1.333);
#if PBR_ENABLED
    vec3 normalF = mix(normal, normal0, smoothstep(far, far * 0.8, radialDepth) * 0.9) + rippleAdd * 0.85f;
    float fresnel = clamp(fresnel_dielectric(viewDir, normalF, ior), 0.0, 1.0) * clamp(mix(waveAttenuation, 0.65, smoothstep(0.0, 17000.0, radialDepth)), 0.0, 1.0) * smoothstep(-0.5, 0.0, reflect(viewDir, normalF).z);
#else
    float fresnel = clamp(fresnel_dielectric(viewDir, mix(normal, normal0, 0.5), ior), 0.0, 1.0);
#endif // PBR_ENABLED

#if !@waterRefraction
    if (isInterior)
        fresnel *= 0.125;
#endif

#if !PBR_ENABLED
    if (!isInterior)
        fresnel = min(fresnel, clamp(fresnel_dielectric(viewDir, normal0, ior), 0.0, 1.0));
#endif

    if (isUnderwater)
    {
        // Underwater the above-water world is seen through the surface, i.e. it
        // lives in the refraction sample (which is wobbled by the waves). So the
        // blend must favour refraction across most of the up-hemisphere and only
        // lean on the reflection/TIR ceiling right at the grazing horizon, where
        // the surface truly turns into a mirror. A low fresnel keeps the real
        // (rippled) houses/sky/trees visible instead of darkening them into the
        // tinted ceiling.
        float upDot = clamp(abs(viewDir.z), 0.0, 1.0);
        fresnel = isInterior ? 0.0 : (1.0 - smoothstep(0.02, 0.22, upDot)) * 0.55;
    }

    sunSpec.a = min(1.0, sunSpec.a * sunWorldDir.z / SUN_SPEC_FADING_THRESHOLD);

#if @waterRefraction
    float shoreFactor = smoothstep(shoreDistanceModifier + 2500.0, 250.0, waterDepthDistorted);
    vec3 waterColor = baseWaterColor.rgb * (shoreFactor * 0.6 + 0.8);
    #if SUB_SURFACE_ABSORPTION_ENABLED
    {
        const float deepFadeStart = 30.0;
        const float deepFadeEnd = 175.0;
        waterColor = mix(waterColor, exp(-vec3(0.012096, 0.012716, 0.01572) * waterDepthDistorted * 0.4), shoreSulphurIntensity * max(shadow, shoreFactor) * (1.0 - clamp((actualWaterDepth - deepFadeStart) / (deepFadeEnd - deepFadeStart), 0.0, 1.0)));
    }
    #endif // SUB_SURFACE_ABSORPTION_ENABLED
#else
    float shoreFactor = 0.0;
    vec3 waterColor = isInterior ? baseWaterColor.rgb * 1.4 : baseWaterColor.rgb * (1.0 - fresnel) * 1.5;
#endif // @waterRefraction

#if WATER_MULTICOLOR_ENABLED
    waterColor += (max(normal1, normal3) * WATER_COLOR_1 + max(normal2, normal4) * WATER_COLOR_2) * multicolorIntensity * (shoreFactor * 0.5 + 0.5);
#endif // WATER_MULTICOLOR_ENABLED

    float foamMask = 0.0;
#if @wobblyShores && FOAM_ENABLED
    if (foamIntensity > 0.0)
    {
        // Cheap coverage masks first: foam only appears on steep enough wave
        // slopes past a short distance, or in shallow water near shore. When
        // neither can contribute we skip the texture fetches below entirely.
        float waveSlope = length(normal0.xy);
        float waveMask = smoothstep(0.08, 0.35, waveSlope);
        waveMask = waveMask * waveMask * waveMask;
        waveMask *= smoothstep(15.0, 50.0, radialDepth);

        float depthOpacity = 0.0;
        if (g_waterDepth < 80.0)
        {
            float shoreFade = smoothstep(0.0, 5.0, g_waterDepth);
            float deepFade  = 1.0 - smoothstep(5.0, 45.0, g_waterDepth);
            depthOpacity = shoreFade * deepFade;
        }

        if (waveMask > 0.0 || depthOpacity > 0.0)
        {
            const float FOAM_OVERALL_SCALE = 0.0005;
            vec2 foamBase = wp.xy * FOAM_OVERALL_SCALE;

            vec2 drift1 = vec2(waterTimer * 0.003,  waterTimer * 0.002);
            vec2 drift2 = vec2(waterTimer * 0.001, -waterTimer * 0.004);
            vec2 drift3 = vec2(-waterTimer * 0.0025, waterTimer * 0.0015);

            vec2 foamUV1 = foamBase * 2.1 + drift1 * 5.5;
            float baseFoam = texture2D(normalMap, foamUV1).a;

            float baseGrayPulse = 0.4 + sin(waterTimer * 2.5) * 0.15;
            float midGrayPulse  = 0.3 + sin(waterTimer * 1.7 + 1.9) * 0.09;

            float result = mix(baseFoam, 0.5, baseGrayPulse);

            vec2 foamUV2 = foamBase * 5.0 + drift2 * 5.0;
            float detail2 = texture2D(normalMap, foamUV2).a;
            result = result * detail2;

            result = mix(result, 0.5, midGrayPulse);

            vec2 foamUV3 = foamBase * 5.5 + drift3 * 6.5;
            float detail3 = texture2D(normalMap, foamUV3).a;
            result = result * detail3;

            const float FOAM_CONTRAST_LOW  = 0.0;
            const float FOAM_CONTRAST_HIGH = 0.4;
            float combinedFoam = smoothstep(FOAM_CONTRAST_LOW, FOAM_CONTRAST_HIGH, result);

            float waveFoam = combinedFoam * waveMask;
            float depthFoam = combinedFoam * depthOpacity;

            foamMask = clamp(max(waveFoam, depthFoam) * foamIntensity, 0.0, 1.0);
            fresnel *= (1.0f - foamMask);
        }
    }
#endif

    float transparencyControl = clamp(waterTransparency, 0.0, 1.4);
#if @waterRefraction
    float absorptionMultiplier = transparencyControl <= 1.0
        ? mix(2.2, 1.0, transparencyControl)
        : mix(1.0, 0.45, transparencyControl - 1.0);
    vec3 sigma = baseWaterColor.a * (1.0 - baseWaterColor.rgb) * absorptionMultiplier;
    vec3 expSigma = min(vec3(1.0), exp(-sigma * waterDepthDistorted));
    vec3 diffuseShadow = mix(vec3(1.0), vec3(shadow), (1.0 - clamp(expSigma, 0.0, 1.0)));
    vec3 diffuse = diffuseShadow * (1.0 - AMBIENT_INTENSITY);
#else
    float diffuse = shadow * (1.0 - AMBIENT_INTENSITY);
#endif
    vec3 sunDiffuse = lcalcDiffuse(0).rgb;

    // -----------------------------------------------------------------------
    // Weather gate: a single 0..1 factor that is ~1 only in clear, sunlit
    // weather and falls to 0 in overcast, fog and rain. Direct-sun highlights
    // (the warm specular track and the decorative sparkle) are multiplied by it
    // so they do not light up when the sky is grey. Inputs:
    //   - directShare: share of hard direct sun vs flat ambient. The engine
    //     collapses the sun's diffuse toward ambient in overcast/fog.
    //   - sunSpec.a : the engine's own sun specular amount.
    //   - rainIntensity / fog distance: extra suppression for rain and fog.
    float wgDirectShare;
    {
        const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);
        float sunLum = dot(sunDiffuse, LUMA);
        float ambLum = dot(gl_LightModel.ambient.rgb, LUMA);
        wgDirectShare = sunLum / max(sunLum + ambLum, 1e-4);
    }
    float clearWeather = smoothstep(0.45, 0.72, wgDirectShare);
    float fogClear = smoothstep(4000.0, 9000.0, gl_Fog.end);
    float weatherGate = clearWeather * (1.0 - clamp(rainIntensity, 0.0, 1.0)) * fogClear;

#if PBR_ENABLED
    vec3 normalS = normalize(mix(normal, normal0, 0.15f) + rippleAdd * 0.4f);
    float specular = 0.0f;

    if (!isUnderwater)
    {
        const bool isDay = true;
        if (!isInterior)
        {
            // Warm "golden" sun tint only in clear weather; fade it toward a
            // neutral, cooler highlight as the sky greys out so overcast water
            // does not show gold glints.
            vec3 warmSun = vec3(1.48f, 0.90f, 0.30f * sunWorldDir.z + 0.3f);
            vec3 dullSun = vec3(0.55f, 0.62f, 0.72f);
            vec3 daySpec = mix(dullSun, warmSun, clearWeather);
            sunSpec.rgb = dot(sunSpec.rgb, vec3(0.2126f, 0.7152f, 0.0722f))
                 * (isDay ? daySpec
                    : vec3(0.10f, 0.15f, 0.20f) * sunWorldDir.z + 0.04f);
        }

        float alphaBase = isInterior ? 0.25f : mix(isDay ? 0.17f : 0.10f, 0.85f, foamMask);
        // Existing ArenaMP Water Roughness now controls the PBR BRDF lobe.
        // 0.22 is the compatibility/default value and leaves the port unchanged.
        float roughnessScale = clamp(waterSurfaceRoughness / 0.22f, 0.09f, 4.55f);
        float alpha = clamp(alphaBase * roughnessScale, 0.02f, 1.0f);
        float NdotV = max(0.0, dot(normalS, -viewDir));
        float NdotL = max(dot(normalS, sunWorldDir), 0.0f);
        vec3 H = normalize(-viewDir + sunWorldDir);
        alpha *= alpha;
        float VdotH = dot(-viewDir, H);
        float NdotH = max(dot(normalS, H), 0.0);

        float ks = Schlick(0.02, 1.0, VdotH) * sunSpec.a;
        float D = TrowbridgeReitz(alpha, NdotH);
        float denom = 2.0 * mix(2.0 * NdotL * NdotV, NdotL + NdotV, alpha);
        float specularBRDF = D / max(denom, 1e-4);
        // Gate the highlight by weather (interiors keep their own lighting).
        specular = ks * specularBRDF * shadow * (isInterior ? 1.0 : weatherGate);
        specular *= clamp(waterHighlightIntensity, 0.0, 2.0);

        diffuse *= (1.0 - ks);
    }
#else
    float NdotL = sunFade * sunFade;
    float specular = pow(max(dot(reflect(viewDir, normal), sunWorldDir), 0.0), SPEC_HARDNESS) * shadow * sunSpec.a * weatherGate
        * clamp(waterHighlightIntensity, 0.0, 2.0);
#endif

    waterColor = waterColor * sunFade * sunFadeSqrt * (diffuse + AMBIENT_INTENSITY);

    vec3 skyColorEstimate = (envSkyStrength > 0.0)
        ? envSkyAwayColor * length(gl_LightModel.ambient.rgb) * 2.0
        : vec3(max(0.0, mix(-0.3, 1.0, sunFade)));
    // Raindrops no longer paint any bright/white ring. The whole impact is
    // carried by the surface normal (dropNormal, folded into the normal below,
    // and into the refraction offset above), so a drop reads as a voluminous but
    // fully transparent dimple that distorts the view through the water rather
    // than a white decal on top of it. Above and below water both stay clear.
    vec3 rainSpecular = vec3(0.0);
    if (isUnderwater)
        rainSpecular = vec3(0.0f);
    float surfaceOpacity = clamp(fresnel * 6.0 + specular, 0.0, 1.0);
    float opacityControl = transparencyControl <= 1.0
        ? mix(1.20, 1.0, transparencyControl)
        : mix(1.0, 0.68, transparencyControl - 1.0);

#if PBR_ENABLED
    normal = mix(normal3, mix(normal2, mix(normal0, normal1, 0.75 * fresnel / (fresnel_dielectric(viewDir, normal1, ior) + fresnel + 0.0001)), smoothstep(80.0, 500.0, radialDepth) * 0.5f + 0.5f), 0.1 * waveAttenuation + 0.85) * (isUnderwater ? -1.0 : 1.0);
#else
    normal = mix(normal3, mix(normal2, mix(normal, normal1, fresnel / (fresnel_dielectric(viewDir, normal1, ior) + fresnel + 0.0001)), smoothstep(80.0, 500.0, radialDepth) * 0.5f + 0.5f), 0.1 * waveAttenuation + 0.60) * (isUnderwater ? -1.0 : 1.0);
#endif

    // Over water: add the steep drop normal so raindrop rings stand up as real
    // 3D dimples in the reflection/refraction distortion, not flat decals.
    if (!isUnderwater)
    {
        normal.xy += dropNormal.xy;
        normal = normalize(normal);
    }

    // Underwater: the refraction normal rebuilt above comes only from the wave
    // layers, so neither the player/actor wake nor the raindrops reach it. Fold
    // both in here (sign-flipped to match the -1.0 underwater normal) so from
    // below you see the player's wake trail and gentle raindrop dimples as
    // transparent distortions, not black rings.
    if (isUnderwater)
        normal.xy += -(playerDistortion.xy + rainRipple.xy * 2.2);

    vec2 reflOffset = normal.xy * REFL_BUMP;
    vec3 reflection = sampleReflectionMap(screenCoords + reflOffset).rgb;

#if WATER_SKY_CONVERGENCE
    if (!isInterior)
    {
        vec3 skyColor;
        if (envSkyStrength > 0.0)
        {
            skyColor = envSkyAwayColor * length(gl_LightModel.ambient.rgb) * 1.5;
            skyColor = max(skyColor, gl_LightModel.ambient.rgb * 1.5);
        }
        else
        {
            skyColor = max(gl_Fog.color.rgb, gl_LightModel.ambient.rgb * 1.5);
        }
        skyColor *= mix(0.5, 1.5, sunFade);
        float distFactor = smoothstep(SKY_CONVERGENCE_START, SKY_CONVERGENCE_END, radialDepth);
        float chopFactor = waveStrength * SKY_CONVERGENCE_CHOP_SCALE;
        float grazeFactor = 1.0 - abs(viewDir.z);
        float skyBlend = clamp(distFactor * chopFactor * grazeFactor, 0.0, 1.0);
        reflection = mix(reflection, skyColor, skyBlend);
    }
#endif // WATER_SKY_CONVERGENCE

    if (!isInterior)
        reflection *= waveAttenuation * 0.5f + 0.5f;

    // CalculateReflectionAttenuation darkens the reflection toward grazing
    // angles to mimic the above-water Fresnel edge. Underwater the surface is a
    // total-internal-reflection mirror, so this must not be applied there or the
    // upper view collapses to black on grazing looks. Only attenuate above water.
    if (!isUnderwater)
    {
#if PBR_ENABLED
        float mss = CalculateReflectionAttenuation(normalF);
        reflection *= mss * mss;
#else
        reflection *= CalculateReflectionAttenuation(normal);
#endif // PBR_ENABLED
    }

    reflection *= (1.0 - rainIntensity * 0.5);

    if (isUnderwater)
    {
        // Grazing / TIR ceiling only. The above-water view comes from the
        // refraction sample via the fresnel blend below, so this term is only
        // what shows at grazing angles where the surface acts as a mirror of the
        // underwater world. There is no underwater reflection texture, so use a
        // smooth, lit, time-of-day-graded water medium colour here.
        float litMedium = AMBIENT_INTENSITY + shadow * (1.0 - AMBIENT_INTENSITY);
        float dayLight = mix(0.20, 1.0, sunFade);
        vec3 mediumCeil = baseWaterColor.rgb * litMedium * dayLight * 2.4;
        mediumCeil = mix(mediumCeil, mediumCeil + WATER_SSS_COLOR * 0.5, sunFade * 0.5);
        reflection = mediumCeil;
    }
    bool isSky = false;

#if @waterRefraction
    vec2 refrCoords = screenCoords - screenCoordsOffset - underwaterRefrOffset;
    vec3 refraction;
    if (isUnderwater)
    {
        // Soften the distorted underwater view with a small 3-tap blur so the
        // refraction does not show hard pixel steps along the wave/wake edges.
        // Blur scale is tied to screen resolution so it stays ~constant on screen.
        vec2 pixelStep = vec2(length(dFdx(screenCoords)), length(dFdy(screenCoords)));
        vec2 blur = max(pixelStep, vec2(1.0 / 4096.0)) * 1.3;
        refraction = sampleRefractionMap(refrCoords).rgb * 0.5
                   + sampleRefractionMap(refrCoords + vec2(blur.x, blur.y)).rgb * 0.25
                   + sampleRefractionMap(refrCoords + vec2(-blur.x, blur.y)).rgb * 0.25;
    }
    else
    {
        refraction = sampleRefractionMap(refrCoords).rgb;
    }
    vec3 rawRefraction = refraction;
    if (isUnderwater)
    {
        refraction = clamp(refraction * (isInterior ? 0.4 : 0.9), 0.0, 1.0);

        const float g = 0.6;
        const float scatteringWeight = 0.77;
        float phase = g * g + 1.0 - 2.0 * g * max(0.0, dot(viewDir, sunWorldDir));
        phase = phase * sqrt(phase) + 0.000001;
        phase = (1.0 - g * g) / (4.0 * 3.14159 * phase);
        vec3 sunScatter = clamp(exp2(-sigma * max(0.0, position.z - cameraPos.z)), 0.0, 1.0);
        sunScatter = vec3(1.0, 0.85, 0.66) * phase * sunScatter * scatteringWeight * sunSpec.a;
        refraction = sunScatter * diffuseShadow + refraction;

        // -------------------------------------------------------------------
        // Underwater fog + tint: distant geometry seen through the water column
        // fades into the active water type's colour, lit by time of day.
        {
            // distance the view ray travels through water to the seen surface.
            float throughWater = max(depthSampleDistorted - surfaceDepth, 0.0);
            // skip fogging the bright sky disc seen through the Snell window.
            float hitsGeometry = 1.0 - smoothstep(far * 0.9, far, depthSampleDistorted);
            // when looking up through the surface at above-water objects the ray
            // barely travels through water, so only fog when looking down/out.
            float looksDown = smoothstep(0.10, -0.10, viewDir.z); // 1 down, 0 up
            // scale absorption up for the underwater medium so the fog closes in
            // at a believable distance.
            vec3 fogTrans = exp(-sigma * throughWater * 9.0);
            fogTrans = clamp(fogTrans, 0.0, 1.0);
            fogTrans = mix(vec3(1.0), fogTrans, hitsGeometry * looksDown);
            // fog colour: the water tint lit by daylight, ambient floor at night.
            float dayLight = mix(0.06, 1.0, sunFade);
            vec3 fogColor = baseWaterColor.rgb * dayLight;
            // a touch of the SSS hue warms the near field.
            fogColor = mix(fogColor, mix(fogColor, WATER_SSS_COLOR, 0.25), sunFade);
            refraction = mix(fogColor, refraction, fogTrans);
        }

        refraction *= (1.0 - foamMask);
        isSky = (depthSampleDistorted >= far && viewDir.z > 0.02f);
    }
    else
    {
        refraction *= refractionBrightness;
        vec3 transmission = (1.0 - foamMask) * expSigma * min(transparencyControl, 1.0);
        refraction = clamp(mix(waterColor, refraction, transmission), 0.0, 1.0);
        // Values above 1.0 progressively restore some of the unabsorbed scene,
        // making clear water visibly clearer without changing the default look.
        float extraClarity = max(transparencyControl - 1.0, 0.0);
        refraction = mix(refraction, rawRefraction * refractionBrightness,
            extraClarity * 0.35 * (1.0 - foamMask));
        refraction = mix(refraction, vec3(dot(refraction, vec3(0.2126, 0.7152, 0.0722))), foamMask);

        // Shadowed water volume: where the surface is in shadow the column below
        // receives no direct sun, so it reads darker and leans toward the water's
        // own absorption colour, making cast shadows feel volumetric.
        {
            // deeper water -> more of the column is shadowed; ramp with depth but
            // saturate so shallows stay clear.
            float depthRamp = smoothstep(20.0, 400.0, waterDepthDistorted);
            float shadowAmt = (1.0 - shadow) * depthRamp;
            refraction *= mix(1.0, 0.62, shadowAmt);
            // cool tint pulled from the water's own colour plus a touch of SSS.
            vec3 shadowTint = mix(baseWaterColor.rgb, WATER_SSS_COLOR, 0.25);
            refraction = mix(refraction, refraction * (0.5 + shadowTint * 2.0), shadowAmt * 0.5);
        }
    }

#if @sunlightScattering && !PBR_ENABLED
    vec3 scatterNormal = (normal1 * bigWaves.x * 0.5 + normal2 * bigWaves.y * 0.5 + normal3 * midWaves.x * 0.2 +
                          normal4 * midWaves.y * 0.2 + normal5Faded * smallWaves.x * 0.1 + normal0 * smallWaves.y * 0.1 + rippleAdd);
    scatterNormal = normalize(vec3(-scatterNormal.xy * bump, scatterNormal.z));
    float sunHeight = sunWorldDir.z;
    vec3 scatterColour = mix(SCATTER_COLOUR * vec3(1.0, 0.4, 0.0), SCATTER_COLOUR, max(1.0 - exp(-sunHeight * SUN_EXT), 0.0));
    float scatterLambert = max(dot(sunWorldDir, scatterNormal) * 0.7 + 0.3, 0.0);
    float scatterReflectAngle = max(dot(reflect(sunWorldDir, scatterNormal), viewDir) * 2.0 - 1.2, 0.0);
    float lightScatter = (shadow * AMBIENT_INTENSITY + 0.5) * scatterLambert * scatterReflectAngle * SCATTER_AMOUNT * sunFade * sunSpec.a * max(1.0 - exp(-sunHeight), 0.0);
    refraction = mix(refraction, scatterColour, lightScatter * (1.0 - foamMask) * smoothstep(0.0, 500.0, waterDepthDistorted));
#endif

    vec3 sss = vec3(0.0);
#if PBR_ENABLED && WATER_SSS_ENABLED
    sss = sssIntensity * clamp(WATER_SSS_COLOR * (1.0 - abs(viewDir.z)) * (1.0f - fresnel) * max(0.0, dot(viewDir, sunWorldDir)), 0.0, 1.0) * shadow * sunSpec.a * smoothstep(0.4, 0.1, foamMask) * smoothstep(0.0, 500.0, actualWaterDepth);
    sss += baseWaterColor.rgb * 0.2f * expSigma * sunSpec.a;
    refraction += sss;
#endif // PBR_ENABLED && WATER_SSS_ENABLED

    gl_FragData[0].rgb = mix(refraction, reflection, fresnel);
    gl_FragData[0].a = 1.0;
    rainSpecular *= surfaceOpacity;
#else
    gl_FragData[0].rgb = mix(waterColor, reflection, fresnel);
    gl_FragData[0].a = clamp((isInterior ? surfaceOpacity * 0.5 + 0.5 : surfaceOpacity * 0.28 + 0.72) * opacityControl, 0.0, 1.0);

    // Without refraction the grazing horizon shows a bright reflected-sky seam.
    // Roll the water back toward its own colour there so the white band fades.
    if (!isInterior && !isUnderwater)
    {
        float horizonBand = smoothstep(0.35, 0.02, abs(viewDir.z));      // strongest at the flat horizon
        horizonBand *= smoothstep(1500.0, 6000.0, radialDepth);          // only in the distance
        gl_FragData[0].rgb = mix(gl_FragData[0].rgb, waterColor, horizonBand * 0.75);
    }
#endif // @waterRefraction

    // ArenaMP keeps the refraction RTT alive even when the user disables the
    // visual refraction option.  Reuse the 0.51 no-refraction look dynamically
    // instead of rebuilding/switching the water pipeline at runtime.
    if (useRefraction < 0.5)
    {
        gl_FragData[0].rgb = mix(waterColor, reflection, fresnel);
        gl_FragData[0].a = clamp((isInterior ? surfaceOpacity * 0.5 + 0.5
                                     : surfaceOpacity * 0.28 + 0.72) * opacityControl, 0.0, 1.0);

        if (!isInterior && !isUnderwater)
        {
            float horizonBand = smoothstep(0.35, 0.02, abs(viewDir.z));
            horizonBand *= smoothstep(1500.0, 6000.0, radialDepth);
            gl_FragData[0].rgb = mix(gl_FragData[0].rgb, waterColor, horizonBand * 0.75);
        }
    }

#if @sunlightScattering
    gl_FragData[0].rgb += (specular * sunSpec.rgb + rainSpecular) * (1.0 - foamMask);
#else
    gl_FragData[0].rgb += rainSpecular * (1.0 - foamMask);
#endif // @sunlightScattering

#if @waterRefraction
    vec3 normalShoreRippleRain = texture2D(normalMap, normalCoords(UV, 2.0, 2.7, -1.0 * waterTimer, 0.05, 0.1, normal4)).rgb - 0.5
                               + texture2D(normalMap, normalCoords(UV, 2.0, 2.7, waterTimer, 0.04, -0.13, normal5)).rgb - 0.5;
    float verticalWaterDepth = max(waterDepthDistorted, 0.0);
    float shoreOffset = verticalWaterDepth - (normal1.x + mix(0.0, normalShoreRippleRain.r, rainIntensity) + 0.15) * 8.0;
    float fuzzFactor = min(1.0, 1000.0 / surfaceDepth);
    shoreOffset *= fuzzFactor;
    shoreOffset = clamp(mix(shoreOffset, 1.0, clamp(linearDepth / WOBBLY_SHORE_FADE_DISTANCE, 0.0, 1.0)), 0.0, 1.0);
    shoreOffset = mix(shoreOffset, 0.0, smoothstep(4.0, 0.0, verticalWaterDepth));
    if (useRefraction > 0.5)
        gl_FragData[0].rgb = mix(gl_FragData[0].rgb, rawRefraction, 1.0f - shoreOffset);
#endif

    if (!isUnderwater)
    {
        float foamLight = clamp(shadow * sunFade + AMBIENT_INTENSITY * sunFade * 0.5, 0.03, 1.0);
        vec3 foamColor = vec3(0.9, 0.9, 0.9) * foamLight;
        gl_FragData[0].rgb = mix(gl_FragData[0].rgb, foamColor, foamMask);
    }

    // -----------------------------------------------------------------------
    // Surface detail layer
    // -----------------------------------------------------------------------
    if (!isUnderwater && !isInterior)
    {
        float sunZ = sunWorldDirUnmodified.z;
        // Sparkle is direct-sun glinting, so it is gated by the same weatherGate
        // computed above (zero in overcast/fog/rain) plus shadow, sun height and
        // the engine's sun specular amount.
        float sparkleGate = shadow * clamp(sunZ * 1.5, 0.0, 1.0) * weatherGate * sunSpec.a;
        if (sparkleGate > 0.001)
        {
            float sparkle = surfaceSparkle(wp.xy, normal, osg_SimulationTime * 3.14);
            vec3 sparkleCol = mix(sunDiffuse, vec3(1.0), 0.4);
            gl_FragData[0].rgb += sparkleCol * sparkle * sparkleGate * (0.18 + 0.12 * fresnel)
                * clamp(waterHighlightIntensity, 0.0, 2.0) * (1.0 - foamMask);
        }
        gl_FragData[0].rgb *= surfaceBreathing(wp.xy, osg_SimulationTime * 3.14);
        vec3 tint = dayTint(sunZ);
        gl_FragData[0].rgb = mix(gl_FragData[0].rgb, gl_FragData[0].rgb * tint, 0.06);
    }

    gl_FragData[0] = applyFogAtDist(gl_FragData[0], radialDepth, linearDepth, far);

#if !@disableNormals
    #if PBR_ENABLED
        normal = normalF * (isUnderwater ? -1.0f : 1.0f);
    #else
        normal = mix(normal, vec3(0.0, 0.0, isUnderwater ? -1.0f : 1.0f), 0.5f);
    #endif // PBR_ENABLED

    gl_FragData[1].rgb = normalize(gl_NormalMatrix * normal) * 0.5 + 0.5;

    #if PBR_ENABLED
        if (isSky)
            gl_FragData[1].rgb = 1.0f - gl_FragData[1].rgb;
        else
        {
            #if @waterRefraction
                gl_FragData[1].rgb *= smoothstep(0.0f, 0.9f, shoreOffset + rainIntensity);
            #endif // @waterRefraction
        }
    #endif // PBR_ENABLED

    gl_FragData[1].rgb *= (1.0f - foamMask) * (min(0.0f, rainSpecular.g) + 1.0f);
#endif // !@disableNormals

#if PBR_ENABLED
#if LIGHTING_DEBUG_MODE == 1
    gl_FragData[0].rgb = isInterior ? vec3(0.0, 0.75, 0.0) : vec3(shadow);
#elif LIGHTING_DEBUG_MODE == 2
    gl_FragData[0].rgb = vec3(1.0);
#elif LIGHTING_DEBUG_MODE == 3
    gl_FragData[0].rgb = sss;
#elif LIGHTING_DEBUG_MODE == 4
    gl_FragData[0].rgb = vec3(UV.xy, 0.0);
#endif // LIGHTING_DEBUG_MODE
#endif // PBR_ENABLED

    applyShadowDebugOverlay();
}
