#define SHADOWS @shadows_enabled

#if SHADOWS
    uniform float maximumShadowMapDistance;
    uniform float shadowFadeStart;
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
        uniform sampler2DShadow shadowTexture@shadow_texture_unit_index;
        varying vec4 shadowSpaceCoords@shadow_texture_unit_index;

#if @perspectiveShadowMaps
        varying vec4 shadowRegionCoords@shadow_texture_unit_index;
#endif
    @endforeach
#endif // SHADOWS

// Runtime shadow filtering. The enhanced path is adapted from the optimized
// PCF ideas in Enhanced Shadows, but keeps ArenaMP's existing cascade selection,
// normal/polygon offsets and arbitrary shadow-map resolution.
uniform float arenaEnhancedShadowFiltering;
uniform float arenaShadowSoftness;
uniform float arenaShadowAdaptiveSoftness;
uniform float arenaShadowTexelSize;

float sampleShadowLegacy(sampler2DShadow shadowMap, vec4 shadowCoords)
{
    float shadow = 0.0;
    vec3 coords = shadowCoords.xyz / shadowCoords.w;
    // Preserve ArenaMP's pre-port sampling exactly when Enhanced Filtering is off.
    // This makes the checkbox a real visual fallback rather than just a lower-quality mode.
    const float texelSize = 0.0008;

    for(float y = -1.0; y <= 1.0; y += 1.0)
    {
        for(float x = -1.0; x <= 1.0; x += 1.0)
        {
            vec2 offset = vec2(x, y) * texelSize;
            shadow += shadow2D(shadowMap, vec3(coords.xy + offset, coords.z)).r;
        }
    }
    return shadow / 9.0;
}

float sampleShadowEnhanced(sampler2DShadow shadowMap, vec4 shadowCoords, float distance)
{
    vec3 coords = shadowCoords.xyz / shadowCoords.w;
    float texel = max(arenaShadowTexelSize, 0.000001);
    float softness = clamp(arenaShadowSoftness, 0.25, 3.0);

#if @limitShadowMapDistance
    float distanceRatio = clamp(distance / max(maximumShadowMapDistance, 1.0), 0.0, 1.0);
#else
    float distanceRatio = clamp(distance / 8192.0, 0.0, 1.0);
#endif
    // Distant softening reduces cascade shimmer while keeping contact shadows
    // crisp. It is runtime-adjustable and does not touch shadow-map generation.
    softness *= mix(1.0, 1.0 + distanceRatio * 0.70,
        clamp(arenaShadowAdaptiveSoftness, 0.0, 1.0));

    vec2 scale = vec2(texel * softness);
    float sum = 0.0;
    // Eight low-discrepancy taps. Each shadow2D lookup can itself use hardware
    // bilinear comparison, giving a much smoother penumbra than the old fixed
    // 3x3 kernel without hard-coding a specific map resolution.
    sum += shadow2D(shadowMap, vec3(coords.xy + vec2( 0.3604,  0.2232) * scale, coords.z)).r;
    sum += shadow2D(shadowMap, vec3(coords.xy + vec2( 0.2434, -0.3508) * scale, coords.z)).r;
    sum += shadow2D(shadowMap, vec3(coords.xy + vec2(-0.3766,  0.2692) * scale, coords.z)).r;
    sum += shadow2D(shadowMap, vec3(coords.xy + vec2(-0.3056, -0.3618) * scale, coords.z)).r;
    sum += shadow2D(shadowMap, vec3(coords.xy + vec2( 0.6034, -0.4808) * scale, coords.z)).r;
    sum += shadow2D(shadowMap, vec3(coords.xy + vec2(-0.5266,  0.4492) * scale, coords.z)).r;
    sum += shadow2D(shadowMap, vec3(coords.xy + vec2( 0.1034,  0.6292) * scale, coords.z)).r;
    sum += shadow2D(shadowMap, vec3(coords.xy + vec2(-0.5766, -0.5208) * scale, coords.z)).r;
    return sum * 0.125;
}

float sampleShadowArena(sampler2DShadow shadowMap, vec4 shadowCoords, float distance)
{
    if (arenaEnhancedShadowFiltering >= 0.5)
        return sampleShadowEnhanced(shadowMap, shadowCoords, distance);
    return sampleShadowLegacy(shadowMap, shadowCoords);
}

float unshadowedLightRatio(float distance)
{
    float shadowing = 1.0;
#if SHADOWS
#if @limitShadowMapDistance
    float fade = clamp((distance - shadowFadeStart) / (maximumShadowMapDistance - shadowFadeStart), 0.0, 1.0);
    if (fade == 1.0)
        return shadowing;
#endif
    #if @shadowMapsOverlap
        bool doneShadows = false;
        @foreach shadow_texture_unit_index @shadow_texture_unit_list
            if (!doneShadows)
            {
                vec3 shadowXYZ = shadowSpaceCoords@shadow_texture_unit_index.xyz / shadowSpaceCoords@shadow_texture_unit_index.w;
#if @perspectiveShadowMaps
                vec3 shadowRegionXYZ = shadowRegionCoords@shadow_texture_unit_index.xyz / shadowRegionCoords@shadow_texture_unit_index.w;
#endif
                if (all(lessThan(shadowXYZ.xy, vec2(1.0, 1.0))) && all(greaterThan(shadowXYZ.xy, vec2(0.0, 0.0))))
                {
                    shadowing = min(sampleShadowArena(shadowTexture@shadow_texture_unit_index,
                        shadowSpaceCoords@shadow_texture_unit_index, distance), shadowing);

                    doneShadows = all(lessThan(shadowXYZ, vec3(0.95, 0.95, 1.0))) && all(greaterThan(shadowXYZ, vec3(0.05, 0.05, 0.0)));
#if @perspectiveShadowMaps
                    doneShadows = doneShadows && all(lessThan(shadowRegionXYZ, vec3(1.0, 1.0, 1.0))) && all(greaterThan(shadowRegionXYZ.xy, vec2(-1.0, -1.0)));
#endif
                }
            }
        @endforeach
    #else
        @foreach shadow_texture_unit_index @shadow_texture_unit_list
            shadowing = min(sampleShadowArena(shadowTexture@shadow_texture_unit_index,
                shadowSpaceCoords@shadow_texture_unit_index, distance), shadowing);
        @endforeach
    #endif
#if @limitShadowMapDistance
    shadowing = mix(shadowing, 1.0, fade);
#endif
#endif // SHADOWS
    return shadowing;
}

void applyShadowDebugOverlay()
{
#if SHADOWS && @useShadowDebugOverlay
    bool doneOverlay = false;
    float colourIndex = 0.0;
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
        if (!doneOverlay)
        {
            vec3 shadowXYZ = shadowSpaceCoords@shadow_texture_unit_index.xyz / shadowSpaceCoords@shadow_texture_unit_index.w;
#if @perspectiveShadowMaps
            vec3 shadowRegionXYZ = shadowRegionCoords@shadow_texture_unit_index.xyz / shadowRegionCoords@shadow_texture_unit_index.w;
#endif
            if (all(lessThan(shadowXYZ.xy, vec2(1.0, 1.0))) && all(greaterThan(shadowXYZ.xy, vec2(0.0, 0.0))))
            {
                colourIndex = mod(@shadow_texture_unit_index.0, 3.0);
		        if (colourIndex < 1.0)
			        gl_FragData[0].x += 0.1;
		        else if (colourIndex < 2.0)
			        gl_FragData[0].y += 0.1;
		        else
			        gl_FragData[0].z += 0.1;

                doneOverlay = all(lessThan(shadowXYZ, vec3(0.95, 0.95, 1.0))) && all(greaterThan(shadowXYZ, vec3(0.05, 0.05, 0.0)));
#if @perspectiveShadowMaps
                doneOverlay = doneOverlay && all(lessThan(shadowRegionXYZ.xyz, vec3(1.0, 1.0, 1.0))) && all(greaterThan(shadowRegionXYZ.xy, vec2(-1.0, -1.0)));
#endif
            }
        }
    @endforeach
#endif // SHADOWS
}