#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#define GROUNDCOVER

#include "water_waves.glsl"

#if @diffuseMap
uniform sampler2D diffuseMap;
varying vec2 diffuseMapUV;
#endif

#if @normalMap
uniform sampler2D normalMap;
varying vec2 normalMapUV;
varying vec4 passTangent;
#endif

// Other shaders respect forcePPL, but legacy groundcover mods were designed to work with vertex lighting.
// They may do not look as intended with per-pixel lighting, so ignore this setting for now.
#define PER_PIXEL_LIGHTING @normalMap

varying float euclideanDepth;
varying float linearDepth;
varying vec3 passWorldPos;

uniform float osg_SimulationTime;
uniform mat4 osg_ViewMatrixInverse;

const float MAX_CAUSTICS_DISTANCE = 2500.0;
const float CAUSTICS_FADE_START = 1500.0;

#if PER_PIXEL_LIGHTING
varying vec3 passViewPos;
varying vec3 passNormal;
#else
centroid varying vec3 passLighting;
centroid varying vec3 shadowDiffuseLighting;
#endif

#include "helpsettings.glsl"
#include "shadows_fragment.glsl"
#include "lighting.glsl"
#include "lighting_enhanced_pbr.glsl"
#include "alpha.glsl"
#include "atmosphere.glsl"

void main()
{
#if @normalMap
    vec4 normalTex = texture2D(normalMap, normalMapUV);

    vec3 normalizedNormal = normalize(passNormal);
    vec3 normalizedTangent = normalize(passTangent.xyz);
    vec3 binormal = cross(normalizedTangent, normalizedNormal) * passTangent.w;
    mat3 tbnTranspose = mat3(normalizedTangent, binormal, normalizedNormal);

    vec3 viewNormal = gl_NormalMatrix * normalize(tbnTranspose * (normalTex.xyz * 2.0 - 1.0));
#endif

#if @diffuseMap
    gl_FragData[0] = texture2D(diffuseMap, diffuseMapUV);
#else
    gl_FragData[0] = vec4(1.0);
#endif

    if (euclideanDepth > @groundcoverFadeStart)
        gl_FragData[0].a *= 1.0-smoothstep(@groundcoverFadeStart, @groundcoverFadeEnd, euclideanDepth);

    alphaTest();

    // Convert to linear space for lighting calculations
    gl_FragData[0].xyz = preLight(gl_FragData[0].xyz);

    float shadowing = unshadowedLightRatio(linearDepth);

    vec3 lighting;
#if !PER_PIXEL_LIGHTING
    lighting = passLighting + shadowDiffuseLighting * shadowing;
#else
    vec3 diffuseLight, ambientLight;
    doLighting(passViewPos, normalize(viewNormal), shadowing, diffuseLight, ambientLight);
#if @materialQuality > 0
    vec3 ignoredGroundcoverSpecular = vec3(0.0);
    arenaApplyEnhancedPbr(passViewPos, normalize(viewNormal), clamp(gl_FragData[0].xyz, 0.0, 1.0),
        clamp(pbrTerrainRoughness, 0.08, 1.0), 0.0, 1.0, 0.80,
        shadowing, 1.0, 0.0,
        diffuseLight, ambientLight, ignoredGroundcoverSpecular);
#endif
    lighting = diffuseLight + ambientLight;
    clampLightingResult(lighting);
#endif

    gl_FragData[0].xyz *= lighting;

    // Apply tonemapping after all lighting calculations
    gl_FragData[0].xyz = toneMap(gl_FragData[0].xyz);


    vec3 cameraPos = (osg_ViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    float cameraWaterH = zDoWaveSimple(cameraPos.xy, osg_SimulationTime);
    bool cameraUnderwater = cameraPos.z < cameraWaterH;

    vec3 sunPos = lcalcPosition(0);
    vec3 sunDir = normalize(sunPos);
    vec3 sunWDir = (osg_ViewMatrixInverse * vec4(sunDir, 0.0)).xyz;
    bool isInterior = sunWDir.y > 0.0;

    float waterH = zDoWaveSimple(passWorldPos.xy, osg_SimulationTime);
    float waterDepth = max(-passWorldPos.z + waterH, 0.0);

    float distanceToFragment = length(passWorldPos.xy - cameraPos.xy);
    float causticsFade = 1.0;
    if (distanceToFragment > CAUSTICS_FADE_START)
        causticsFade = 1.0 - smoothstep(CAUSTICS_FADE_START, MAX_CAUSTICS_DISTANCE, distanceToFragment);

    if (!isInterior && passWorldPos.z < waterH && waterDepth > 2.0 && distanceToFragment < MAX_CAUSTICS_DISTANCE) {
        float causticsIntensity = zcaustics(passWorldPos.xy * 0.012, osg_SimulationTime * 0.5) * 1.70;
        float causticsBlend = clamp(waterDepth * 0.018, 0.0, 0.72) / (1.0 + waterDepth / 700.0);
        causticsBlend *= causticsFade;
        gl_FragData[0].xyz *= mix(1.0, 0.65 + causticsIntensity, causticsBlend);
    }

    if (cameraUnderwater && !isInterior && waterDepth > 0.0)
        gl_FragData[0].xyz = applyUnderwaterMedium(gl_FragData[0].xyz, waterDepth, isInterior);

#if @radialFog
    float fogValue = clamp((euclideanDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#else
    float fogValue = clamp((linearDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#endif
    vec3 arenaWorldFogDirection = passWorldPos - cameraPos;
    vec3 arenaBlendedFog = arenaFogColour(gl_Fog.color.xyz, arenaWorldFogDirection);
    gl_FragData[0].xyz = mix(gl_FragData[0].xyz, arenaBlendedFog, fogValue);

    applyShadowDebugOverlay();
}
