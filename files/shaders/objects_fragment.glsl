#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "water_waves.glsl"

#if @diffuseMap
uniform sampler2D diffuseMap;
varying vec2 diffuseMapUV;
#endif

#if @darkMap
uniform sampler2D darkMap;
varying vec2 darkMapUV;
#endif

#if @detailMap
uniform sampler2D detailMap;
varying vec2 detailMapUV;
#endif

#if @decalMap
uniform sampler2D decalMap;
varying vec2 decalMapUV;
#endif

#if @emissiveMap
uniform sampler2D emissiveMap;
varying vec2 emissiveMapUV;
#endif

#if @normalMap
uniform sampler2D normalMap;
varying vec2 normalMapUV;
varying vec4 passTangent;
#endif

#if @envMap
uniform sampler2D envMap;
varying vec2 envMapUV;
uniform vec4 envMapColor;
#endif

#if @specularMap
uniform sampler2D specularMap;
varying vec2 specularMapUV;
#endif

#if @bumpMap
uniform sampler2D bumpMap;
varying vec2 bumpMapUV;
uniform vec2 envMapLumaBias;
uniform mat2 bumpMapMatrix;
#endif

uniform bool simpleWater;
uniform bool noAlpha;
uniform bool isInterior;
uniform bool isInventoryPreview;
uniform float waterCausticsIntensity;
uniform float waterUnderwaterTint;
uniform float waterUnderwaterBlend;

varying float euclideanDepth;
varying float linearDepth;

#define PER_PIXEL_LIGHTING (@normalMap || @forcePPL)

#if !PER_PIXEL_LIGHTING
centroid varying vec3 passLighting;
centroid varying vec3 shadowDiffuseLighting;
#else
uniform float emissiveMult;
#endif
varying vec3 passViewPos;
varying vec3 passNormal;

uniform float osg_SimulationTime;
uniform mat4 osg_ViewMatrixInverse;

#include "helpsettings.glsl"
#include "vertexcolors.glsl"
#include "shadows_fragment.glsl"
#define ARENAMP_FRAGMENT_SHADER 1
#include "lighting.glsl"
#include "lighting_enhanced_pbr.glsl"
#include "parallax.glsl"
#include "alpha.glsl"

// ==========================================================================
// ПАРАМЕТРЫ ОГРАНИЧЕНИЯ КАУСТИКИ ПО ДИСТАНЦИИ
// ==========================================================================
const float MAX_CAUSTICS_DISTANCE = 2500.0;  // Максимальная дистанция для каустики
const float CAUSTICS_FADE_START = 1500.0;    // Начало плавного затухания
// ==========================================================================

void main()
{
#if @diffuseMap
    vec2 adjustedDiffuseUV = diffuseMapUV;
#endif
#if @normalMap
    vec2 adjustedNormalUV = normalMapUV;
#endif
#if @specularMap
    vec2 adjustedSpecularUV = specularMapUV;
#endif

#if @normalMap
    vec4 normalTex = texture2D(normalMap, adjustedNormalUV);

    vec3 normalizedNormal = normalize(passNormal);
    vec3 normalizedTangent = normalize(passTangent.xyz);
    vec3 binormal = cross(normalizedTangent, normalizedNormal) * passTangent.w;
    mat3 tbnTranspose = mat3(normalizedTangent, binormal, normalizedNormal);

    vec3 viewNormal = gl_NormalMatrix * normalize(tbnTranspose * pbrSafeTangentNormal(normalTex.xyz));
#endif

#if (!@normalMap && (@parallax || @forcePPL))
    vec3 viewNormal = gl_NormalMatrix * normalize(passNormal);
#endif

#if @parallax && @materialQuality >= 2
    float parallaxDirectVisibility = 1.0;
    float parallaxAmbientVisibility = 1.0;
    float parallaxRawHeight = normalTex.a;
    vec3 parallaxCameraPos = (gl_ModelViewMatrixInverse * vec4(0,0,0,1)).xyz;
    vec3 objectPos = (gl_ModelViewMatrixInverse * vec4(passViewPos, 1)).xyz;
    vec3 eyeDir = normalize(parallaxCameraPos - objectPos);
    vec3 lightDirObject = normalize((gl_ModelViewMatrixInverse
        * vec4(normalize(lcalcPosition(0)), 0.0)).xyz);
    vec2 offset = getMaterialParallaxOffset(eyeDir, lightDirObject, tbnTranspose,
        normalMap, adjustedNormalUV, (passTangent.w > 0.0) ? -1.0 : 1.0,
        length(passViewPos), parallaxRawHeight,
        parallaxDirectVisibility, parallaxAmbientVisibility);
#if @diffuseMap
    vec2 diffuseUvDelta = diffuseMapUV - normalMapUV;
    if (dot(diffuseUvDelta, diffuseUvDelta) < 0.00000025)
        adjustedDiffuseUV += offset;
#endif
    adjustedNormalUV += offset;
#if @specularMap
    vec2 specularUvDelta = specularMapUV - normalMapUV;
    if (dot(specularUvDelta, specularUvDelta) < 0.00000025)
        adjustedSpecularUV += offset;
#endif
    normalTex = texture2D(normalMap, adjustedNormalUV);
    viewNormal = gl_NormalMatrix * normalize(tbnTranspose * pbrSafeTangentNormal(normalTex.xyz));
#endif

#if @diffuseMap
    gl_FragData[0] = texture2D(diffuseMap, adjustedDiffuseUV);
    gl_FragData[0].a *= coveragePreservingAlphaScale(diffuseMap, adjustedDiffuseUV);
#else
    gl_FragData[0] = vec4(1.0);
#endif

#if @materialQuality > 0 && @specularMap
    vec4 arenaSpecTex = texture2D(specularMap, adjustedSpecularUV);
    // Auto-detection of packed PBR parameter maps is disabled for the ArenaMW
    // legacy content path. Old Morrowind specular/environment maps and their
    // distant mip levels were being misclassified as packed material data,
    // causing dark/black blotches that disappeared when the player moved
    // closer. If explicit packed-PBR support is needed later, it should be a
    // deliberate opt-in path rather than a heuristic on arbitrary textures.
    bool arenaPackedPbr = false;
    float arenaMaterialAO = 1.0;
#endif

    // Runtime PBR material parameters. Packed PBR maps follow the Enhanced PBR
    // convention: R=metallicity, G=roughness, B=AO, A=inverse SSS. Legacy
    // specular maps are estimated conservatively so old texture packs stay sane.
    float arenaPbrRoughness = clamp(pbrObjectRoughness, 0.08, 1.0);
    float arenaPbrMetallicity = 0.0;
    float arenaPbrAO = 1.0;
    float arenaPbrSSS = 0.0;
    float arenaLegacyMaterialFactor = 1.0;
#if @materialQuality > 0 && @specularMap
    if (arenaPackedPbr)
    {
        arenaPbrRoughness = clamp(arenaSpecTex.g, 0.08, 1.0);
        arenaPbrMetallicity = clamp(arenaSpecTex.r, 0.0, 1.0);
        arenaPbrAO = arenaMaterialAO;
        arenaPbrSSS = clamp(1.0 - arenaSpecTex.a, 0.0, 1.0);
        arenaLegacyMaterialFactor = 0.0;
    }
    else
    {
        float arenaLegacySpecLuma = dot(arenaSpecTex.rgb, vec3(0.2126, 0.7152, 0.0722));
        float arenaLegacySmoothness = clamp(arenaSpecTex.a * 0.65 + arenaLegacySpecLuma * 0.35, 0.0, 1.0);
        float arenaLegacyRoughnessFloor = isInterior ? 0.58 : 0.42;
        arenaPbrRoughness = max(mix(arenaPbrRoughness, 0.22, arenaLegacySmoothness * 0.72), arenaLegacyRoughnessFloor);
        arenaPbrAO = mix(1.0, clamp(0.88 + arenaLegacySpecLuma * 0.12, 0.0, 1.0), 0.35);
        arenaPbrSSS = 0.0;
    }
#else
    arenaPbrRoughness = max(arenaPbrRoughness, isInterior ? 0.58 : 0.42);
#endif
    vec3 arenaEnhancedSpecular = vec3(0.0);

    vec4 diffuseColor = getDiffuseColor();
    gl_FragData[0].a *= diffuseColor.a;
    alphaTest();

#if @detailMap
    gl_FragData[0].xyz *= texture2D(detailMap, detailMapUV).xyz * 2.0;
#endif

#if @darkMap
    gl_FragData[0].xyz *= texture2D(darkMap, darkMapUV).xyz;
#endif

#if @decalMap
    vec4 decalTex = texture2D(decalMap, decalMapUV);
    gl_FragData[0].xyz = mix(gl_FragData[0].xyz, decalTex.xyz, decalTex.a);
#endif

#if @envMap

    vec2 envTexCoordGen = envMapUV;
    float envLuma = 1.0;

#if @normalMap
    // if using normal map + env map, take advantage of per-pixel normals for envTexCoordGen
    vec3 viewVec = normalize(passViewPos.xyz);
    vec3 r = reflect( viewVec, viewNormal );
    float m = 2.0 * sqrt( r.x*r.x + r.y*r.y + (r.z+1.0)*(r.z+1.0) );
    envTexCoordGen = vec2(r.x/m + 0.5, r.y/m + 0.5);
#endif

#if @bumpMap
    vec4 bumpTex = texture2D(bumpMap, bumpMapUV);
    envTexCoordGen += bumpTex.rg * bumpMapMatrix;
    envLuma = clamp(bumpTex.b * envMapLumaBias.x + envMapLumaBias.y, 0.0, 1.0);
#endif

#if @preLightEnv
    #if @materialQuality > 0
    gl_FragData[0].xyz += texture2D(envMap, envTexCoordGen).xyz * envMapColor.xyz * envLuma * 0.22;
#else
    gl_FragData[0].xyz += texture2D(envMap, envTexCoordGen).xyz * envMapColor.xyz * envLuma;
#endif
#endif

#endif

    float shadowing = unshadowedLightRatio(linearDepth);
    
    vec3 lighting;
#if !PER_PIXEL_LIGHTING
    lighting = passLighting + shadowDiffuseLighting * shadowing;
#else
    vec3 diffuseLight, ambientLight;
    doLighting(passViewPos, normalize(viewNormal), shadowing, diffuseLight, ambientLight);
#if @parallax && @materialQuality >= 2
    diffuseLight *= parallaxDirectVisibility;
    ambientLight *= parallaxAmbientVisibility;
#endif
#if @materialQuality > 0
    vec3 arenaPbrAlbedo = clamp(gl_FragData[0].xyz * diffuseColor.xyz, 0.0, 1.0);
    arenaApplyEnhancedPbr(passViewPos, normalize(viewNormal), arenaPbrAlbedo,
        arenaPbrRoughness, arenaPbrMetallicity, arenaPbrAO, arenaPbrSSS,
        shadowing, arenaLegacyMaterialFactor, isInterior ? 1.0 : 0.0,
        diffuseLight, ambientLight, arenaEnhancedSpecular);
#endif
#if @materialQuality >= 4 && @specularMap
    if (arenaPackedPbr && pbrEnhancedLighting < 0.5)
        ambientLight *= arenaMaterialAO;
#endif
    vec3 emission = getEmissionColor().xyz * emissiveMult;
    lighting = diffuseColor.xyz * diffuseLight + getAmbientColor().xyz * ambientLight + emission;
    clampLightingResult(lighting);
#endif
    
    gl_FragData[0].xyz *= lighting;

#if @envMap && !@preLightEnv
    #if @materialQuality > 0
    gl_FragData[0].xyz += texture2D(envMap, envTexCoordGen).xyz * envMapColor.xyz * envLuma * 0.22;
#else
    gl_FragData[0].xyz += texture2D(envMap, envTexCoordGen).xyz * envMapColor.xyz * envLuma;
#endif
#endif

    // Convert to linear space for lighting calculations
    gl_FragData[0].xyz = preLight(gl_FragData[0].xyz);

#if @emissiveMap
    gl_FragData[0].xyz += texture2D(emissiveMap, emissiveMapUV).xyz;
#endif

#if @materialQuality > 0
#if @materialQuality > 0 && PER_PIXEL_LIGHTING
    if (pbrEnhancedLighting >= 0.5)
    {
        gl_FragData[0].xyz += arenaEnhancedSpecular;
    }
    else
#endif
    {
#if @specularMap
    float shininess;
    vec3 matSpec;
    if (arenaPackedPbr)
    {
        float roughness = pbrPackedRoughness(arenaSpecTex);
        shininess = pbrShininessFromRoughness(roughness);
#if @materialQuality >= 4
        // Legacy fallback for packed parameter maps. Enhanced PBR uses the full
        // R=metal, G=roughness, B=AO, A=inverse-SSS convention above.
        matSpec = vec3(0.007);
#else
        float smoothness = 1.0 - roughness;
        matSpec = vec3(0.004 + 0.016 * smoothness * smoothness);
#endif
    }
    else
    {
        shininess = clamp(arenaSpecTex.a * 255.0, 1.0, 192.0);
        float legacyLuma = dot(arenaSpecTex.rgb, vec3(0.2126, 0.7152, 0.0722));
        matSpec = mix(vec3(min(legacyLuma, 0.045)), clamp(arenaSpecTex.rgb, 0.0, 0.07), 0.06);
    }
#else
    float shininess = clamp(gl_FrontMaterial.shininess, 1.0, 192.0);
    vec3 matSpec = clamp(getSpecularColor().xyz, 0.0, 0.045);
#endif

    if (dot(matSpec, matSpec) > 0.000001)
    {
#if (!@normalMap && !@parallax && !@forcePPL)
        vec3 viewNormal = gl_NormalMatrix * normalize(passNormal);
#endif
        gl_FragData[0].xyz += getSpecular(normalize(viewNormal), normalize(passViewPos.xyz), shininess, matSpec) * shadowing;
    }
    }
#endif

    // Apply tonemapping after all lighting calculations
    gl_FragData[0].xyz = toneMap(gl_FragData[0].xyz);

    // ==========================================================================
    // OPTIMIZED UNDERWATER WAVE EFFECTS FOR OBJECTS
    // С ОГРАНИЧЕНИЕМ ПО ДИСТАНЦИИ
    // ==========================================================================
    
    // Проверяем позицию КАМЕРЫ
    vec3 cameraPos = (osg_ViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    // CPU-side water-plane hysteresis supplies a stable immediate 0/1 underwater state.
    // Do not compare the camera against animated waves per fragment: that made
    // the tint alternate on/off near the surface.
    
    // isInterior is supplied from the authoritative active-cell state.
    
    vec3 wPos = (osg_ViewMatrixInverse * vec4(passViewPos, 1.0)).xyz;
    float waterH = zDoWaveSimple(wPos.xy, osg_SimulationTime);
    float waterDepth = max(-wPos.z + waterH, 0.0);

    // ==========================================================================
    // ОГРАНИЧЕНИЕ КАУСТИКИ ПО ДИСТАНЦИИ
    // ==========================================================================
    // Рассчитываем дистанцию от камеры до фрагмента
    float distanceToFragment = length(wPos.xy - cameraPos.xy);
    
    // Плавное затухание каустики на дальних расстояниях
    float causticsFade = 1.0;
    if (distanceToFragment > CAUSTICS_FADE_START) {
        causticsFade = 1.0 - smoothstep(CAUSTICS_FADE_START, MAX_CAUSTICS_DISTANCE, distanceToFragment);
    }
    // ==========================================================================

    // OPTIMIZED: Simplified caustics with depth check and distance fade
#if (OBJECT_CAUSTICS == 1)
    if (!isInterior && !isInventoryPreview && wPos.z < waterH && waterDepth > 5.0 && distanceToFragment < MAX_CAUSTICS_DISTANCE) {
        float causticsIntensity = zcaustics(wPos.xy * 0.01, osg_SimulationTime * 0.5) * 1.55;
        float causticsBlend = clamp(waterDepth * 0.010, 0.0, 0.94) / (1.0 + waterDepth / 1100.0);
        
        // Применяем плавное затухание по дистанции
        causticsBlend *= causticsFade;
        
        gl_FragData[0].xyz *= mix(1.0, 0.5 + causticsIntensity * waterCausticsIntensity, causticsBlend);
    }
#endif

    // Применяем attenuation ТОЛЬКО если камера под водой
    if (waterUnderwaterBlend > 0.001 && !isInterior && !isInventoryPreview && waterDepth > 0.0) {
#if (ATTENUATION == 1)
        float stableTint = clamp(waterUnderwaterTint * waterUnderwaterBlend, 0.0, 2.0);
        gl_FragData[0].xyz = mix(gl_FragData[0].xyz,
            applyUnderwaterMedium(gl_FragData[0].xyz, waterDepth, isInterior), stableTint);
#endif
    }

    // ==========================================================================
    // END UNDERWATER EFFECTS
    // ==========================================================================

#if @radialFog
    float depth;
    // For the less detailed mesh of simple water we need to recalculate depth on per-pixel basis
    if (simpleWater)
        depth = length(passViewPos);
    else
        depth = euclideanDepth;
    float fogValue = clamp((depth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#else
    float fogValue = clamp((linearDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#endif
    gl_FragData[0].xyz = mix(gl_FragData[0].xyz, gl_Fog.color.xyz, fogValue);

#if @translucentFramebuffer
    // having testing & blending isn't enough - we need to write an opaque pixel to be opaque
    if (noAlpha)
        gl_FragData[0].a = 1.0;
#endif

    applyShadowDebugOverlay();
}
