#ifndef ARENAMP_MATERIAL_PARALLAX
#define ARENAMP_MATERIAL_PARALLAX

// ArenaMP material-quality profiles (compile-time shader define):
// 0 None      - normal/specular maps are disabled by the renderer.
// 1 Simple    - normal maps only, no UV displacement.
// 2 Balanced  - inexpensive iterative parallax.
// 3 Quality   - POM with moderate self-shadowing.
// 4 Ultra     - full POM, contact refinement, self-shadowing and cavity AO.

#define PARALLAX_NEUTRAL_HEIGHT 0.5
#define PARALLAX_SCALE_TERRAIN 0.04
#define PARALLAX_SCALE_OBJECTS 0.04

#if @materialQuality == 2
    #define ARENAMP_POM_MIN_LAYERS 4.0
    #define ARENAMP_POM_MAX_LAYERS 8.0
    #define ARENAMP_POM_REFINEMENT 0
    #define ARENAMP_SHADOW_STEPS 0
#elif @materialQuality == 3
    #define ARENAMP_POM_MIN_LAYERS 6.0
    #define ARENAMP_POM_MAX_LAYERS 24.0
    #define ARENAMP_POM_REFINEMENT 3
    #define ARENAMP_SHADOW_STEPS 5
#else
    #define ARENAMP_POM_MIN_LAYERS 8.0
    #define ARENAMP_POM_MAX_LAYERS 32.0
    #define ARENAMP_POM_REFINEMENT 5
    #define ARENAMP_SHADOW_STEPS 10
#endif

float arenaParallaxScale()
{
#ifdef TERRAIN
    return PARALLAX_SCALE_TERRAIN;
#else
    return PARALLAX_SCALE_OBJECTS;
#endif
}

vec3 arenaToTangent(vec3 direction, mat3 tbnTranspose, float flipY)
{
    vec3 tangentDirection = normalize(direction * tbnTranspose);
    tangentDirection.y *= flipY;
    return tangentDirection;
}

vec2 arenaIterativeParallax(sampler2D heightMap, vec2 baseUV, vec3 eyeTS,
    float scale, int iterations, out float rawHeight)
{
    float safeZ = max(abs(eyeTS.z), 0.10);
    vec2 viewOffset = (eyeTS.xy / safeZ) * scale;
    vec2 offset = vec2(0.0);
    rawHeight = texture2D(heightMap, baseUV).a;

    // Fixed upper bound keeps this valid on GLSL 1.20 drivers.
    for (int i = 0; i < 4; ++i)
    {
        if (i >= iterations)
            break;
        rawHeight = texture2D(heightMap, baseUV + offset).a;
        offset = viewOffset * (rawHeight - PARALLAX_NEUTRAL_HEIGHT);
    }
    return offset;
}

vec2 arenaPomParallax(sampler2D heightMap, vec2 baseUV, vec3 eyeTS,
    float scale, out float rawHeight)
{
    float safeZ = max(abs(eyeTS.z), 0.08);
    vec2 ray = (eyeTS.xy / safeZ) * scale;
    float layerCount = mix(ARENAMP_POM_MAX_LAYERS, ARENAMP_POM_MIN_LAYERS,
        clamp(abs(eyeTS.z), 0.0, 1.0));
    vec2 deltaUV = ray / layerCount;
    float layerStep = 1.0 / layerCount;

    // Bidirectional height maps use 0.5 as the geometric surface.
    vec2 currentUV = baseUV + ray * PARALLAX_NEUTRAL_HEIGHT;
    float currentLayer = 1.0;
    rawHeight = texture2D(heightMap, currentUV).a;

    for (int i = 0; i < 32; ++i)
    {
        if (float(i) >= layerCount || currentLayer < rawHeight)
            break;
        currentUV -= deltaUV;
        currentLayer -= layerStep;
        rawHeight = texture2D(heightMap, currentUV).a;
    }

#if ARENAMP_POM_REFINEMENT > 0
    vec2 refineUV = deltaUV * 0.5;
    float refineLayer = layerStep * 0.5;
    currentUV += refineUV;
    currentLayer += refineLayer;

    for (int i = 0; i < ARENAMP_POM_REFINEMENT; ++i)
    {
        refineUV *= 0.5;
        refineLayer *= 0.5;
        rawHeight = texture2D(heightMap, currentUV).a;
        if (currentLayer < rawHeight)
        {
            currentUV += refineUV;
            currentLayer += refineLayer;
        }
        else
        {
            currentUV -= refineUV;
            currentLayer -= refineLayer;
        }
    }
#endif

    rawHeight = texture2D(heightMap, currentUV).a;
    return currentUV - baseUV;
}

float arenaParallaxShadow(sampler2D heightMap, vec2 uv, float currentHeight,
    vec3 lightTS, float scale)
{
#if ARENAMP_SHADOW_STEPS == 0
    return 1.0;
#else
    if (lightTS.z <= 0.02)
        return 1.0;

    float remainingHeight = max(1.0 - currentHeight, 0.0);
    if (remainingHeight <= 0.01)
        return 1.0;

    float stepHeight = remainingHeight / float(ARENAMP_SHADOW_STEPS);
    vec2 stepUV = (lightTS.xy / max(lightTS.z, 0.05)) * scale * stepHeight;
    float rayHeight = currentHeight + stepHeight * 0.15;
    float visibility = 1.0;

    for (int i = 1; i <= ARENAMP_SHADOW_STEPS; ++i)
    {
        uv += stepUV;
        rayHeight += stepHeight;
        if (rayHeight >= 1.0)
            break;
        float blocker = texture2D(heightMap, uv).a - rayHeight;
        if (blocker > 0.0)
        {
            float distanceFactor = max(float(i) / float(ARENAMP_SHADOW_STEPS), 0.1);
            visibility = min(visibility,
                1.0 - clamp(blocker * (2.2 / distanceFactor), 0.0, 0.82));
        }
    }
    return visibility;
#endif
}

float arenaParallaxAO(sampler2D heightMap, vec2 uv, float currentHeight, float scale)
{
#if @materialQuality < 4
    return 1.0;
#else
    float radiusNear = scale * 0.30;
    float radiusFar = scale * 1.20;
    float occlusion = 0.0;

    occlusion += max(0.0, texture2D(heightMap, uv + vec2( radiusNear, 0.0)).a - currentHeight);
    occlusion += max(0.0, texture2D(heightMap, uv + vec2(-radiusNear, 0.0)).a - currentHeight);
    occlusion += max(0.0, texture2D(heightMap, uv + vec2(0.0,  radiusNear)).a - currentHeight);
    occlusion += max(0.0, texture2D(heightMap, uv + vec2(0.0, -radiusNear)).a - currentHeight);
    occlusion += 0.5 * max(0.0, texture2D(heightMap, uv + vec2( radiusFar,  radiusFar)).a - currentHeight);
    occlusion += 0.5 * max(0.0, texture2D(heightMap, uv + vec2(-radiusFar,  radiusFar)).a - currentHeight);
    occlusion += 0.5 * max(0.0, texture2D(heightMap, uv + vec2( radiusFar, -radiusFar)).a - currentHeight);
    occlusion += 0.5 * max(0.0, texture2D(heightMap, uv + vec2(-radiusFar, -radiusFar)).a - currentHeight);

    return clamp(exp2(-occlusion * 4.0), 0.35, 1.0);
#endif
}

vec2 getMaterialParallaxOffset(vec3 eyeDir, vec3 lightDir, mat3 tbnTranspose,
    sampler2D heightMap, vec2 baseUV, float flipY, float viewDistance,
    out float rawHeight, out float directVisibility, out float ambientVisibility)
{
    vec3 eyeTS = arenaToTangent(eyeDir, tbnTranspose, flipY);
    vec3 lightTS = arenaToTangent(lightDir, tbnTranspose, flipY);
    float scale = arenaParallaxScale();
    vec2 offset = vec2(0.0);
    rawHeight = texture2D(heightMap, baseUV).a;
    directVisibility = 1.0;
    ambientVisibility = 1.0;

#if @materialQuality == 2
    int iterations = viewDistance < 4200.0 ? 3 : 1;
    offset = arenaIterativeParallax(heightMap, baseUV, eyeTS, scale, iterations, rawHeight);
#elif @materialQuality == 3
    if (viewDistance < 4200.0)
        offset = arenaPomParallax(heightMap, baseUV, eyeTS, scale, rawHeight);
    else
        offset = arenaIterativeParallax(heightMap, baseUV, eyeTS, scale, 2, rawHeight);
    directVisibility = arenaParallaxShadow(heightMap, baseUV + offset,
        rawHeight, lightTS, scale);
#elif @materialQuality >= 4
    if (viewDistance < 5200.0)
        offset = arenaPomParallax(heightMap, baseUV, eyeTS, scale, rawHeight);
    else
        offset = arenaIterativeParallax(heightMap, baseUV, eyeTS, scale, 2, rawHeight);
    directVisibility = arenaParallaxShadow(heightMap, baseUV + offset,
        rawHeight, lightTS, scale);
    ambientVisibility = arenaParallaxAO(heightMap, baseUV + offset, rawHeight, scale);
#endif

    // Avoid extreme UV stretching at grazing angles and fade displacement at range.
    float angleFade = smoothstep(0.03, 0.18, abs(eyeTS.z));
#if @materialQuality == 2
    float distanceFade = 1.0 - smoothstep(5000.0, 6500.0, viewDistance);
#elif @materialQuality == 3
    float distanceFade = 1.0 - smoothstep(6500.0, 8000.0, viewDistance);
#else
    float distanceFade = 1.0 - smoothstep(7500.0, 9500.0, viewDistance);
#endif
    float fade = angleFade * distanceFade;
    directVisibility = mix(1.0, directVisibility, fade);
    ambientVisibility = mix(1.0, ambientVisibility, fade);
    return offset * fade;
}

#endif
