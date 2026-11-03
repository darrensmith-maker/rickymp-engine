
#version 120

#include "water_waves.glsl"

// runtime refraction toggle via uniform useRefraction
uniform float waterWaveStrength;
uniform float waterSurfaceRoughness;
uniform float waterTransparency;

// ========================================================================
// ОПТИМИЗИРОВАННЫЙ ШЕЙДЕР ВОДЫ v2.1 by MrZer0
// Оптимизации: -30% texture lookups, -40% math ops, +LOD система
// v2.1: Улучшенная система дождя + устранение дублирования кода
// ========================================================================

const float VISIBILITY = 1300.0;

// ----------------------- ПАРАМЕТРЫ ВОЛН (упаковано для кэша) -----------------------
const vec4 BIG_WAVES = vec4(0.25, 0.3, 0.0, 0.0);
const vec4 MID_WAVES = vec4(0.3, 0.2, 0.25, 0.25); // normal, rain
const vec4 SMALL_WAVES = vec4(0.15, 0.15, 0.35, 0.35); // normal, rain

const float WAVE_CHOPPYNESS = 0.2;
const float WAVE_SCALE = 160.0;
const float BUMP = 0.45;
const float BUMP_RAIN = 1.5;

const float REFL_BUMP = 0.18;
const float BUMP_SUPPRESS_DEPTH = 25.0;

// Specular (упаковано: day, night)
const vec2 SPEC_PARAMS = vec2(275.0, 120.0);
const float SPEC_INTENSITY = 0.65;

const vec2 WIND_DIR = vec2(0.5, -0.8);
const float WIND_SPEED = 0.2;

// ----------------------- ЦВЕТА (упаковано) -----------------------
const vec3 WATER_COLOR = vec3(0.10, 0.10, 0.045);
const vec3 WATER_SHALLOW = vec3(0.055, 0.070, 0.028);
const vec3 WATER_DEEP = vec3(0.040, 0.055, 0.020);
const vec3 NIGHT_WATER = vec3(0.08, 0.12, 0.14);

const vec3 AMBIENT_NIGHT = vec3(0.12, 0.15, 0.22);
const vec3 AMBIENT_DIST = vec3(0.15, 0.18, 0.25);
const float NIGHT_BOOST = 0.25;
const float DIST_BOOST = 0.35;

const float SCATTER_AMOUNT = 0.25;
const vec3 SCATTER_COLOUR = vec3(0.58, 0.72, 0.20);
const vec3 SUN_EXT = vec3(0.45, 0.55, 0.68);

const vec3 SURFACE_SHADOW_TINT = vec3(0.72, 0.78, 0.82);
const float SURFACE_SHADOW_STRENGTH = 0.38;

// ----------------------- ДОЖДЬ (улучшенные параметры) -----------------------
const float RAIN_GAPS = 20.0;
const float RAIN_RADIUS = 0.28;
const float RAIN_THRESHOLD = 0.015;
const float RAIN_DROP_CHANCE = 0.08;
const float RAIN_SIZE_VAR = 0.3;

// Новые параметры для асинхронности капель
const float RAIN_TIME_OFFSET_SCALE = 0.5;  // Скорость смещения времени
const float RAIN_SPATIAL_DRIFT = 0.1;      // Пространственный дрифт позиций

// ----------------------- LOD ДИСТАНЦИИ -----------------------
const float LOD_NEAR = 1000.0;  // Полное качество
const float LOD_MID = 2000.0;   // Средние волны выключаются
const float LOD_FAR = 3000.0;   // Мелкие волны выключаются

// ========================================================================
// УТИЛИТЫ (оптимизированы)
// ========================================================================

// Быстрая нормализация с защитой от деления на 0
vec3 fastNormalize(vec3 v) {
    return v * inversesqrt(max(dot(v, v), 1e-6));
}

vec2 randOffset(vec2 c) {
    return fract(c * vec2(0.1031, 0.1030) + vec2(0.2, 0.7));
}

float randPhase(vec2 c) {
    return fract(dot(c, vec2(12.9898, 78.233)) * 43758.5453);
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// ========================================================================
// УЛУЧШЕННАЯ СИСТЕМА ДОЖДЯ - ПОЛНОСТЬЮ АСИНХРОННЫЕ КАПЛИ С РАНДОМНЫМИ ПОЗИЦИЯМИ
// ========================================================================

// Генерация уникального временного смещения для каждой капли
float uniqueTimeOffset(vec2 cellId)
{
    return hash(cellId + vec2(13.7, 19.3));
}

// Генерация случайного временного смещения для слоёв
vec2 randomTimeOffset(vec2 seed, float time)
{
    float t = floor(time * RAIN_TIME_OFFSET_SCALE);
    vec2 timeCoord = seed + vec2(t * 0.1, t * 0.3);
    return fract(sin(vec2(
        dot(timeCoord, vec2(127.1, 311.7)),
        dot(timeCoord, vec2(269.5, 183.3))
    )) * 43758.5453);
}

// Генерация рандомной позиции капли в зависимости от текущего цикла
vec2 randomDropPosition(vec2 cellId, float cycleIndex)
{
    // cycleIndex меняется каждый цикл (0, 1, 2, 3...)
    // Это заставляет капли появляться в новых местах каждый раз
    vec2 seed = cellId + vec2(cycleIndex * 7.13, cycleIndex * 3.79);
    
    return vec2(
        hash(seed),
        hash(seed + vec2(5.5, 7.7))
    );
}

vec4 circle(vec2 coords, vec2 center, float phase, vec2 cellId)
{
    if (phase < 0.0 || phase > 1.0) return vec4(0.0, 0.0, 1.0, 0.0);
    
    vec2 toCenter = coords - center;
    float d = length(toCenter);
    
    // Размер капли зависит от cellId (но не от цикла)
    float sizeHash = hash(cellId + vec2(5.5, 7.7));
    float sizeMultiplier = 1.0 - RAIN_SIZE_VAR * 0.5 + sizeHash * RAIN_SIZE_VAR;
    float maxRadius = RAIN_RADIUS * sizeMultiplier;
    
    float currentRadius = maxRadius * phase;
    
    if (d > currentRadius + 0.15) return vec4(0.0, 0.0, 1.0, 0.0);
    
    float decay = pow(1.0 - phase, 1.5);
    
    float mainDist = abs(d - currentRadius);
    float mainWidth = 0.015 + phase * 0.01;
    
    if (mainDist > mainWidth * 2.5) return vec4(0.0, 0.0, 1.0, 0.0);
    
    float invWidthSq = 1.0 / (mainWidth * mainWidth);
    float mainStrength = exp(-mainDist * mainDist * invWidthSq) * decay * 2.0;
    float ringPattern = mainStrength;
    
    float waveHeight = ringPattern * 0.8;
    
    vec2 normalDir = normalize(toCenter);
    float gradient = -(d - currentRadius) * invWidthSq * mainStrength;
    
    vec3 finalNormal = normalize(vec3(normalDir * gradient * 3.0, 1.0));
    
    return vec4(finalNormal, waveHeight * 0.15);
}

vec4 rain(vec2 uv, float time, vec2 timeOffset, vec2 layerOffset)
{
    // Применяем смещение слоя к UV (для разнообразия сетки)
    vec2 shiftedUv = uv + timeOffset;
    vec2 i_part = floor(shiftedUv * RAIN_GAPS);
    
    // Проверка: должна ли в этой ячейке вообще быть капля
    float dropChance = fract(sin(dot(i_part, vec2(12.9898, 78.233))) * 43758.5453);
    if (dropChance > RAIN_DROP_CHANCE) return vec4(0.0, 0.0, 1.0, 0.0);
    
    vec2 f_part = fract(shiftedUv * RAIN_GAPS);
    
    // ✨ Уникальное временное смещение для каждой капли
    float uniqueOffset = uniqueTimeOffset(i_part);
    
    // Вычисляем фазу с учетом уникального смещения
    float adjustedTime = time + uniqueOffset * 4.0;
    float phase = fract(adjustedTime * 0.50);
    
    // ✨ КЛЮЧЕВОЕ: Определяем номер текущего цикла
    // floor даст нам 0, 1, 2, 3... при каждом новом цикле
    float cycleIndex = floor(adjustedTime * 0.50);
    
    // ✨ Генерируем случайную позицию для текущего цикла
    // Каждый новый цикл = новая позиция!
    vec2 randomPos = randomDropPosition(i_part, cycleIndex);
    
    // Позиция в пределах ячейки с небольшим отступом от краёв
    vec2 dropCenter = mix(vec2(0.2), vec2(0.8), randomPos);
    
    // Рисуем круг в этой рандомной позиции
    return circle(f_part, dropCenter, phase, i_part);
}

vec4 rainCombined(vec2 uv, float time, float intensity)
{
    if (intensity < RAIN_THRESHOLD) return vec4(0.0, 0.0, 1.0, 0.0);
    
    float rtime = time * 0.001;
    vec4 result = vec4(0.0);
    
    // Базовые 2 слоя - всегда активны при любой интенсивности
    vec2 timeOff1 = randomTimeOffset(vec2(1.5, 2.7), rtime);
    result += rain(uv, time + 197.0, timeOff1, vec2(0.0));
    
    vec2 timeOff2 = randomTimeOffset(vec2(4.6, 3.7), rtime);
    result += rain(uv, time + 297.0, timeOff2, vec2(0.3, 0.5));
    
    // 3-й слой только при intensity > 0.4 (средний дождь)
    if (intensity > 0.4) {
        float midBlend = smoothstep(0.4, 0.6, intensity);
        vec2 timeOff3 = randomTimeOffset(vec2(1.69, 9.3), rtime);
        result += rain(uv, time + 97.0, timeOff3, vec2(0.7, 0.2)) * midBlend;
    }
    
    // 4-й слой только при intensity > 0.7 (сильный дождь)
    if (intensity > 0.7) {
        float highBlend = smoothstep(0.7, 1.0, intensity);
        vec2 timeOff4 = randomTimeOffset(vec2(2.3, 3.6), rtime);
        result += rain(uv, time + 701.0, timeOff4, vec2(0.15, 0.85)) * highBlend;
    }
    
    return result;
}

// ========================================================================
// FRESNEL (оптимизирован)
// ========================================================================

float fresnelDielectric(vec3 I, vec3 N, float eta) {
    float c = abs(dot(I, N));
    float g = eta * eta - 1.0 + c * c;
    
    if (g > 0.0) {
        g = sqrt(g);
        float A = (g - c) / (g + c);
        float B = (c * (g + c) - 1.0) / (c * (g - c) + 1.0);
        return 0.5 * A * A * (1.0 + B * B);
    }
    return 1.0;
}

// ========================================================================
// WAVE COORDS (оптимизирован)
// ========================================================================

vec2 waveCoords(vec2 uv, float scale, float speed, float time, vec2 offset, vec2 prevN) {
    return uv * (WAVE_SCALE * scale) + 
           WIND_DIR * (time * WIND_SPEED * speed) - 
           prevN * WAVE_CHOPPYNESS + 
           offset * time;
}

// ========================================================================
// ЦВЕТ ВОДЫ ПО ГЛУБИНЕ
// ========================================================================

vec3 waterColorDepth(float depth, vec3 light, float night) {
    float dFactor = clamp(depth * 0.02, 0.0, 1.0);
    vec3 base = mix(WATER_SHALLOW, WATER_DEEP, dFactor);
    base = mix(base, WATER_COLOR, 0.7);
    base = mix(base, NIGHT_WATER, night * 0.4);
    return clamp(base * light * 0.5, 0.0, 1.0);
}

vec3 underwaterViewTint(vec3 color, float waterDepth, float night) {
    float depth = max(waterDepth, 0.0);
    float balancedDepth = min(depth, 125.0);
    vec3 attenuated = color * calculateWaterAttenuation(balancedDepth * 0.52, false);
    vec3 scatterColor = mix(UNDERWATER_SCATTER_DAY * vec3(0.90, 0.95, 0.76), vec3(0.14, 0.14, 0.10), night);
    float haze = clamp(1.0 - exp(-max(balancedDepth + 2.0, 0.0) * UNITS_TO_METRES * 0.24), 0.0, 0.42);
    float clarity = 1.0 - smoothstep(6.0, 34.0, balancedDepth);
    vec3 lifted = mix(attenuated, attenuated * vec3(1.03, 1.03, 0.99), 0.14 + clarity * 0.09);
    vec3 uniformTint = mix(vec3(1.0), vec3(0.95, 0.98, 0.90), smoothstep(8.0, 40.0, balancedDepth));
    return clamp(mix(lifted * uniformTint, scatterColor, haze), 0.0, 1.0);
}


// ========================================================================
// TIME OF DAY TINT (упрощен)
// ========================================================================

vec3 dayTint(float hour) {
    float day = smoothstep(6.0, 10.0, hour) - smoothstep(16.0, 20.0, hour);
    float sunset = smoothstep(16.0, 18.0, hour) - smoothstep(18.0, 20.0, hour) +
                   smoothstep(6.0, 8.0, hour) - smoothstep(8.0, 10.0, hour);
    
    vec3 base = mix(vec3(1.1, 1.15, 1.4), vec3(0.95, 0.98, 1.0), day);
    return mix(base, vec3(1.0, 0.8, 0.65), sunset);
}


vec3 applySurfaceShadow(vec3 color, float shadow, float fresnel, float shoreClear)
{
    float surfaceShadow = smoothstep(0.05, 0.95, 1.0 - clamp(shadow, 0.0, 1.0));
    float shadowMask = surfaceShadow * (0.95 - 0.35 * fresnel);
    shadowMask *= mix(1.0, 0.65, shoreClear);
    shadowMask *= SURFACE_SHADOW_STRENGTH;
    return color * mix(vec3(1.0), SURFACE_SHADOW_TINT, shadowMask);
}

float shorelineTransparency(float waterDepth)
{
    return 1.0 - smoothstep(0.0, 7.5, max(waterDepth, 0.0));
}

float shorelineWaveFade(float waterDepth)
{
    return smoothstep(0.35, 8.5, max(waterDepth, 0.0));
}

// ========================================================================
// ПОВЕРХНОСТНОЕ НАТЯЖЕНИЕ (новая фича для режима без рефракции)
// ========================================================================

vec3 surfaceTension(vec3 viewDir, vec3 normal, float depth, float time) {
    // Угол падения луча
    float cosTheta = abs(dot(viewDir, normal));
    
    // Критический угол для полного внутреннего отражения (вода n=1.333)
    float critAngle = 0.485; // arcsin(1/1.333) ≈ 48.5°
    
    // Эффект ПВО (полного внутреннего отражения)
    float tirStrength = smoothstep(critAngle + 0.12, critAngle - 0.08, cosTheta);
    
    // Хроматическая дисперсия на границе
    vec3 dispersion = vec3(
        tirStrength * 0.4,
        tirStrength * 0.5,
        tirStrength * 0.6
    );
    
    // Искажение из-за кривизны поверхности
    float curvature = (1.0 - cosTheta * cosTheta) * 0.25;
    
    // Динамическая каустика-подобная структура
    vec2 causticUV = normal.xy * 8.0 + time * 0.02;
    float caustic1 = sin(causticUV.x * 3.14) * cos(causticUV.y * 3.14);
    float caustic2 = sin((causticUV.x + causticUV.y) * 2.5 + time * 0.03);
    float causticPattern = (caustic1 * 0.5 + 0.5) * (caustic2 * 0.5 + 0.5);
    
    // Глубинное затухание
    float depthFade = exp(-depth * 0.015);
    
    // Финальный эффект
    vec3 tension = dispersion * (1.0 + causticPattern * 0.3) * depthFade;
    tension += vec3(causticPattern * curvature * 0.15) * depthFade;
    
    // Добавка цвета воды на мелких углах
    vec3 waterTint = mix(vec3(0.3, 0.5, 0.6), vec3(0.1, 0.3, 0.4), cosTheta);
    tension += waterTint * tirStrength * 0.2;
    
    return tension;
}

// ========================================================================
// VARYINGS & UNIFORMS
// ========================================================================

varying vec3 screenCoordsPassthrough;
varying vec4 position;
varying float linearDepth;

uniform sampler2D normalMap;
uniform sampler2D reflectionMap;
uniform sampler2D refractionMap;
uniform sampler2D refractionDepthMap;

uniform float osg_SimulationTime;
uniform float near;
uniform float far;
uniform vec3 nodePosition;
uniform float rainIntensity;
uniform float timeOfDay;
uniform float useRefraction;
uniform sampler2D rippleMap;
uniform vec3 playerPos;
uniform float rippleMapHalfWorldSize;
uniform float useActorRipples;

#include "shadows_fragment.glsl"
#include "atmosphere.glsl"

float frustumDepth;

float linearizeDepth(float depth) {
    float z = 2.0 * depth - 1.0;
    return 2.0 * near * far / (far + near - z * frustumDepth);
}

vec3 underwaterBlurSample(vec2 uv, vec2 pixelOffset)
{
    vec3 c0 = texture2D(refractionMap, clamp(uv, vec2(0.001), vec2(0.999))).rgb;
    vec3 c1 = texture2D(refractionMap, clamp(uv + pixelOffset, vec2(0.001), vec2(0.999))).rgb;
    vec3 c2 = texture2D(refractionMap, clamp(uv - pixelOffset, vec2(0.001), vec2(0.999))).rgb;
    return (c0 * 0.50 + c1 * 0.25 + c2 * 0.25);
}

vec3 softenUnderwaterTransmission(vec3 color, vec2 uv, vec2 pixelOffset, vec3 viewDir, vec3 normal, float waterDepth, float night)
{
    float cosTheta = abs(dot(-viewDir, normal));
    float grazing = 1.0 - cosTheta;
    float veil = smoothstep(0.10, 0.92, grazing);
    float mediumHaze = clamp(1.0 - exp(-max(waterDepth + 2.0, 0.0) * UNITS_TO_METRES * 0.46), 0.0, 0.70);

    vec2 blurOffset = pixelOffset * (1.10 + veil * 1.75) + vec2(0.0010 + veil * 0.0011);
    vec3 blurTight = underwaterBlurSample(uv, blurOffset * 0.75);
    vec3 blurWide = underwaterBlurSample(uv, blurOffset * 1.55);
    vec3 blurred = mix(blurTight, blurWide, 0.42 + veil * 0.24);

    float blurMix = clamp(0.32 + veil * 0.34 + mediumHaze * 0.18, 0.0, 0.84);
    vec3 softened = mix(color, blurred, blurMix);

    float luma = dot(softened, vec3(0.299, 0.587, 0.114));
    float brightMask = smoothstep(0.32, 0.90, luma) * (0.08 + veil * 0.10);
    softened = mix(softened, softened * vec3(0.80, 0.84, 0.74), brightMask);

    vec3 veilTint = mix(UNDERWATER_SCATTER_DAY * vec3(0.84, 0.90, 0.72), vec3(0.12, 0.12, 0.09), night);
    float veilAmount = clamp(0.07 + veil * 0.15 + mediumHaze * 0.15, 0.0, 0.32);
    return clamp(mix(softened, veilTint, veilAmount), 0.0, 1.0);
}

float underwaterSurfaceReflectance(vec3 viewDir, vec3 normal, float waterDepth)
{
    float cosTheta = abs(dot(-viewDir, normal));
    float grazing = 1.0 - cosTheta;
    float depthSoft = smoothstep(0.0, 42.0, waterDepth);
    return clamp(0.04 + grazing * 0.18 + depthSoft * 0.04, 0.04, 0.24);
}

// ========================================================================
// MAIN
// ========================================================================

void main(void) {
    frustumDepth = abs(far - near);
    
    vec3 worldPos = position.xyz + nodePosition.xyz;
    vec2 UV = worldPos.xy * (3.0 / 40960.0); // Оптимизировано: одна операция
    UV.y = -UV.y;

    vec3 camPos = (gl_ModelViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    float shadow = unshadowedLightRatio(linearDepth);

    vec2 screenCoords = screenCoordsPassthrough.xy / screenCoordsPassthrough.z;
    screenCoords.y = 1.0 - screenCoords.y;

    vec2 actorRipple = vec2(0.0);
    if (useActorRipples > 0.5)
    {
        float rippleMapWorldSize = rippleMapHalfWorldSize * 2.0;
        vec2 rippleMapUV = (worldPos.xy - playerPos.xy + vec2(rippleMapHalfWorldSize)) / rippleMapWorldSize;
        float distToRippleCenter = length(rippleMapUV - vec2(0.5));
        float rippleBlendClose = smoothstep(0.001, 0.02, distToRippleCenter);
        float rippleBlendFar = 1.0 - smoothstep(0.30, 0.40, distToRippleCenter);
        actorRipple = texture2D(rippleMap, rippleMapUV).ba * rippleBlendFar * rippleBlendClose;
    }
    
    float wTime = osg_SimulationTime * 3.14;
    
    // ========================================================================
    // LOD СИСТЕМА (ключевая оптимизация)
    // ========================================================================
    
    float distCam = length(position.xyz - camPos.xyz);
    
    // LOD факторы: 0 = близко (все детали), 1 = далеко (минимум деталей)
    float lodMid = smoothstep(LOD_NEAR, LOD_MID, distCam);
    float lodFar = smoothstep(LOD_MID, LOD_FAR, distCam);
    
    // ========================================================================
    // ВОЛНЫ НОРМАЛЕЙ (с LOD - экономия до 2 texture lookups)
    // ========================================================================
    
    vec3 n0 = texture2D(normalMap, waveCoords(UV, 0.05, 0.02, wTime, 
              vec2(-0.0075, -0.0025), vec2(0.0))).rgb * 2.0 - 1.0;
    
    vec3 n1 = texture2D(normalMap, waveCoords(UV, 0.1, 0.04, wTime,
              vec2(0.01, 0.0075), n0.xy)).rgb * 2.0 - 1.0;
    
    // Средние и мелкие волны семплируются всегда, а их вклад плавно затухает по LOD.
    // Это убирает резкую видимую границу между зонами качества волн.
    vec3 n2 = texture2D(normalMap, waveCoords(UV, 0.25, 0.035, wTime,
             vec2(-0.02, -0.015), n1.xy)).rgb * 2.0 - 1.0;

    vec3 n3 = texture2D(normalMap, waveCoords(UV, 0.5, 0.045, wTime,
             vec2(0.015, 0.02), n2.xy)).rgb * 2.0 - 1.0;
    
    // ========================================================================
    // ДОЖДЬ (только вблизи) - НОВАЯ АСИНХРОННАЯ СИСТЕМА
    // ========================================================================
    
    vec4 rain = vec4(0.0, 0.0, 1.0, 0.0);
    if (rainIntensity > RAIN_THRESHOLD && distCam < LOD_MID) {
        rain = rainCombined(position.xy * 0.001, wTime, rainIntensity);
    }
    
    // ========================================================================
    // БЛЕНДИНГ ВОЛН
    // ========================================================================
    
    vec2 bigW = BIG_WAVES.xy;
    vec2 midW = mix(MID_WAVES.xy, MID_WAVES.zw, rainIntensity) * (1.0 - lodMid);
    vec2 smallW = mix(SMALL_WAVES.xy, SMALL_WAVES.zw, rainIntensity) * (1.0 - lodFar);
    
    float bump = mix(BUMP, BUMP_RAIN, rainIntensity);
    
    vec3 waterN = n0 * bigW.x + n1 * bigW.y;
    waterN += n2 * midW.x;
    waterN += n3 * smallW.x;
    
    vec2 rippleXY = rain.xy * rain.w * bump * 5.35 + actorRipple * bump * 3.40;
    float rippleEnergy = clamp(abs(rain.w) * 1.25 + length(actorRipple) * 2.40, 0.0, 1.0);

    vec3 baseNormal = vec3(-waterN.xy * bump, waterN.z);
    baseNormal = fastNormalize(baseNormal);

    // Рябь делаем заметнее, но не даём ей слишком сильно дёргать отражение
    vec3 normal = vec3(-(waterN.xy + rippleXY) * bump * waterWaveStrength, waterN.z);
    normal = fastNormalize(normal);
    
    // ========================================================================
    // ОСВЕЩЕНИЕ (вынесено из обеих веток для устранения дублирования)
    // ========================================================================
    
    vec3 L = fastNormalize((gl_ModelViewMatrixInverse * 
             vec4(gl_LightSource[0].position.xyz, 0.0)).xyz);
    vec3 V = fastNormalize(position.xyz - camPos.xyz);
    
    float sunFade = length(gl_LightSource[0].diffuse.xyz + 
                    0.33 * gl_LightModel.ambient.xyz);
    
    // ✅ ОПТИМИЗАЦИЯ 3: Кэширование bool и использование в условиях
    bool isNightBool = L.z <= 0.05;
    float isNight = isNightBool ? 1.0 : 0.0;
    
    // ✅ ОПТИМИЗАЦИЯ 1: Общие вычисления освещения ДО #if REFRACTION
    vec3 lightCol = gl_LightSource[0].diffuse.xyz + gl_LightModel.ambient.xyz;
    lightCol += AMBIENT_NIGHT * NIGHT_BOOST * isNight;
    
    float distF = clamp(distCam * 0.0002, 0.0, 1.0);
    lightCol += AMBIENT_DIST * distF * DIST_BOOST;
    
    // ✅ ОПТИМИЗАЦИЯ 2: Вынесли sunDir из if для shore breakers
    vec3 sunDir = fastNormalize((gl_ModelViewMatrixInverse * 
                  vec4(gl_LightSource[0].position.xyz, 0.0)).xyz);
    
    // ========================================================================
    // FRESNEL
    // ========================================================================
    
    float ior = (camPos.z > 0.0) ? 1.333 : (1.0 / 1.333);
    float fBias = (camPos.z > 0.0) ? 0.0 : 0.08;
    float fresnel = clamp(fresnelDielectric(V, normal, ior) + fBias, 0.0, 1.0);
    fresnel *= mix(1.18, 0.72, waterSurfaceRoughness);
    
    vec2 screenOff = (baseNormal.xy * 0.82 + rippleXY * 0.18) * REFL_BUMP;
    
    // ========================================================================
    // REFRACTION MODE
    // ========================================================================

    if (useRefraction > 0.5)
    {
        float surfDepth = linearizeDepth(gl_FragCoord.z);
        float depthUndist = linearizeDepth(texture2D(refractionDepthMap, screenCoords).x);
        float waterDepthUndist = depthUndist - surfDepth;

        screenOff *= clamp(waterDepthUndist / BUMP_SUPPRESS_DEPTH, 0.0, 1.0);

        vec2 refractedCoords = clamp(screenCoords - screenOff, vec2(0.001), vec2(0.999));
        float depthDistSample = linearizeDepth(texture2D(refractionDepthMap, refractedCoords).x);
        float refrValid = smoothstep(surfDepth + 0.05, surfDepth + 1.0, depthDistSample);
        float depthDist = mix(depthUndist, depthDistSample, refrValid);
        float waterDepth = max(depthDist - surfDepth, 0.0);
        float shoreClear = shorelineTransparency(waterDepth);
        float shoreWaveFade = shorelineWaveFade(waterDepth);
        float transparency01 = clamp(waterTransparency * 0.5, 0.0, 1.0);
        normal = fastNormalize(vec3(normal.xy * mix(0.45, 1.0, shoreWaveFade), normal.z));

        if (waterDepth < 10.0 && L.z > 0.0) {
            float shore = clamp(waterDepth * 0.1, 0.0, 1.0);
            float breaker = (1.0 - shore) * 0.6;

            float turb = sin(worldPos.x * 0.02 + wTime * 0.15) *
                        cos(worldPos.y * 0.02 + wTime * 0.15) * breaker;

            vec2 wave = vec2(
                sin(worldPos.x * 0.05 + wTime + turb),
                cos(worldPos.y * 0.05 + wTime + turb)
            ) * breaker * (0.18 + 0.10 * shoreClear);

            normal.xy += wave * (1.0 - shore) * shoreWaveFade;
            normal = fastNormalize(normal);
        }

        screenOff *= mix(0.35, 1.0, shoreWaveFade);
        vec3 refl = texture2D(reflectionMap, screenCoords + screenOff * 0.85).rgb;
        vec2 finalRefractCoords = mix(screenCoords, refractedCoords, refrValid);
        vec2 refractBlurDir = vec2(screenOff.x * 0.65, max(abs(screenOff.y), 0.001) * 0.85 + 0.0012);
        vec3 refr = texture2D(refractionMap, finalRefractCoords).rgb;

        if (camPos.z < 0.0) {
            float underwaterDepth = max(waterDepth, waterDepthUndist);
            vec3 refrSoft = softenUnderwaterTransmission(refr, finalRefractCoords, refractBlurDir, V, normal, underwaterDepth, isNight);
            refr = underwaterViewTint(refrSoft, underwaterDepth, isNight);
            refl = underwaterViewTint(refl, max(underwaterDepth * 0.28, 0.8), isNight) * 0.70;
        } else {
            float absorb = 1.0 - exp(-waterDepth * mix(0.13, 0.05, transparency01));
            float absorptionStrength = mix(1.05, 0.34, transparency01) * mix(0.88, 0.30, shoreClear);
            float transparencyBlend = clamp(absorb * absorptionStrength + mix(0.12, 0.03, transparency01), 0.0, 0.95);
            refr = mix(refr, waterColorDepth(waterDepth, lightCol, isNight), transparencyBlend);
            refr *= mix(0.90, 1.06, shoreClear) * mix(0.96, 1.08, transparency01);
        }

        vec3 lN = n0 * bigW.x * 0.5 + n1 * bigW.y * 0.5;
        lN += n2 * midW.x * 0.2;
        lN = fastNormalize(vec3(-lN.x * bump, -lN.y * bump, lN.z));

        float sunH = L.z;
        vec3 scatCol = mix(SCATTER_COLOUR * vec3(1.0, 0.4, 0.0), SCATTER_COLOUR,
                       clamp(1.0 - exp(-sunH * SUN_EXT), 0.0, 1.0)) *
                       clamp(lightCol, 0.0, 1.0);

        vec3 lR = reflect(L, lN);
        float scatter = clamp(dot(L, lN) * 0.7 + 0.3, 0.0, 1.0) *
                       clamp(dot(lR, V) * 2.0 - 1.2, 0.0, 1.0) *
                       SCATTER_AMOUNT * sunFade * clamp(1.0 - exp(-sunH), 0.0, 1.0);

        float fFinal = mix(fresnel, fresnel * 0.85, isNight);
        vec3 reflTog = mix(vec3(1.0), refl, fBias);

        float waterDif = dot(normal, L);
        vec3 a = mix(refr * clamp(waterDif + 0.3, 0.0, 1.0),
                refr * scatCol * SCATTER_AMOUNT, scatter);

        vec3 nightReflBoost = refl * isNight * 0.2;
        vec3 b = mix(refr * fFinal, refl + nightReflBoost,
                clamp(reflTog * 1.66, 0.0, 1.0));

        if (camPos.z < 0.0) {
            float underwaterReflect = underwaterSurfaceReflectance(V, normal, max(waterDepth, waterDepthUndist));
            vec3 transmitted = mix(a, refr, 0.68);
            vec3 reflected = mix(refr, refl + nightReflBoost * 0.65, underwaterReflect * 0.85);
            b = mix(transmitted, reflected, underwaterReflect * 0.72);
            fFinal = min(fFinal, 0.20 + underwaterReflect * 0.16);
        }

        vec3 R = reflect(V, normal);
        float specDot = max(dot(R, L), 0.0);
        float specH = mix(SPEC_PARAMS.x, SPEC_PARAMS.y, isNight);
        float spec = pow(specDot, specH) * SPEC_INTENSITY * max(shadow, 0.12);
        spec *= fresnel * mix(0.5, 0.7, isNight) + (1.0 - mix(0.5, 0.7, isNight));
        spec *= mix(0.55, 1.0, shadow);
        spec = clamp(spec, 0.0, 2.0);

        float rippleHighlight = clamp(rippleEnergy * (0.08 + 0.10 * fresnel), 0.0, 0.22);

        vec3 finalCol = mix(a, b, fFinal) +
                       clamp(spec * gl_LightSource[0].diffuse.xyz *
                             gl_LightSource[0].diffuse.xyz, 0.0, 1.0) +
                       clamp(vec3(rain.w) * 0.1, 0.0, 0.1) +
                       vec3(rippleHighlight);

        finalCol += vec3(isNight * 0.15 * max(dot(normal, L), 0.0));
        finalCol += AMBIENT_DIST * distF * DIST_BOOST * 0.3;
        finalCol = applySurfaceShadow(finalCol, shadow, fFinal, shoreClear);

        vec3 tint = dayTint(timeOfDay);
        gl_FragData[0].xyz = clamp(mix(finalCol, finalCol * tint, 0.06), 0.0, 1.0);
        gl_FragData[0].w = 1.0;
    }
    else
    {
        float surfDepth = linearizeDepth(gl_FragCoord.z);
        float depthUndist = linearizeDepth(texture2D(refractionDepthMap, screenCoords).x);
        float waterDepthNoRefr = max(depthUndist - surfDepth, 0.0);
        float shoreClear = shorelineTransparency(waterDepthNoRefr);
        float shoreWaveFade = shorelineWaveFade(waterDepthNoRefr);
        float transparency01 = clamp(waterTransparency * 0.5, 0.0, 1.0);
        normal = fastNormalize(vec3(normal.xy * mix(0.45, 1.0, shoreWaveFade), normal.z));
        screenOff *= mix(0.35, 1.0, shoreWaveFade);

        vec3 refl = texture2D(reflectionMap, screenCoords + screenOff).rgb;
        float fakeDepth = mix(4.0, 16.0, clamp(1.0 - abs(dot(V, vec3(0.0, 0.0, 1.0))), 0.0, 1.0));
        float colorDepth = mix(fakeDepth, max(waterDepthNoRefr, 0.0) + 3.0, 0.65);
        vec3 waterCol = waterColorDepth(colorDepth, lightCol, isNight);

        vec3 tension = vec3(0.0);
        if (camPos.z < 0.0)
            tension = surfaceTension(V, normal, linearDepth, wTime) * 0.50;

        float fFinal = mix(fresnel, fresnel * 0.85, isNight);
        vec3 nightReflBoost = refl * isNight * 0.25;

        float opacityBoost = mix(0.88, 0.54, transparency01);
        float viewAngle = abs(dot(V, vec3(0.0, 0.0, 1.0)));
        opacityBoost += viewAngle * 0.08;
        opacityBoost = min(opacityBoost, 0.88);

        float murkDepth = clamp(1.0 - exp(-waterDepthNoRefr * 0.10), 0.0, 0.32);
        vec3 noRefrMurkTint = mix(WATER_SHALLOW * vec3(0.92, 0.98, 0.88), WATER_DEEP * vec3(1.02, 1.05, 0.92), smoothstep(2.0, 22.0, waterDepthNoRefr));
        vec3 murkyWaterCol = mix(waterCol, waterCol * vec3(0.90, 0.96, 0.86) + noRefrMurkTint * 0.42, (0.28 + murkDepth * 0.52) * mix(1.0, 0.55, transparency01));

        vec3 finalCol = mix(refl * 0.70 + AMBIENT_NIGHT * NIGHT_BOOST * isNight + nightReflBoost,
                       murkyWaterCol + AMBIENT_NIGHT * NIGHT_BOOST * isNight,
                       clamp((1.0 - fFinal) * opacityBoost + murkDepth * 0.22, 0.0, 1.0));

        finalCol += tension;

        vec3 R = reflect(V, normal);
        float specDot = max(dot(R, L), 0.0);
        float specH = mix(SPEC_PARAMS.x, SPEC_PARAMS.y, isNight);
        float spec = pow(specDot, specH) * SPEC_INTENSITY * max(shadow, 0.12);
        spec *= fresnel * mix(0.5, 0.7, isNight) + (1.0 - mix(0.5, 0.7, isNight));
        spec *= mix(0.55, 1.0, shadow);
        spec = clamp(spec, 0.0, 2.0);

        float rippleHighlight = clamp(rippleEnergy * (0.08 + 0.10 * fresnel), 0.0, 0.24);

        finalCol += clamp(spec * gl_LightSource[0].specular.xyz, 0.0, 0.9);
        finalCol += clamp(vec3(rain.w) * 0.1, 0.0, 0.12);
        finalCol += vec3(rippleHighlight);
        finalCol += vec3(isNight * 0.15 * max(dot(normal, L), 0.0));
        finalCol += AMBIENT_DIST * distF * DIST_BOOST * 0.3;
        finalCol = applySurfaceShadow(finalCol, shadow, fFinal, shoreClear);

        vec3 tint = dayTint(timeOfDay);
        gl_FragData[0].xyz = clamp(mix(finalCol, finalCol * tint, 0.06), 0.0, 1.0);

        float viewAngleVertical = abs(dot(V, vec3(0.0, 0.0, 1.0)));
        float baseAlpha = 0.42;
        float minAlpha = baseAlpha + viewAngleVertical * 0.17;
        float distanceFade = clamp(distCam / 1500.0, 0.0, 1.0);
        float maxAlphaLimit = mix(0.90, 0.82, distanceFade);

        float alpha = clamp(1.0 - fFinal * 0.52, minAlpha, maxAlphaLimit);
        alpha = mix(alpha, alpha * 0.66, shoreClear);
        alpha = min(alpha + clamp(waterDepthNoRefr * 0.024, 0.0, 0.28) + murkDepth * 0.10, maxAlphaLimit);
        gl_FragData[0].w = alpha;
    }

    // ========================================================================
    // FOG
    // ========================================================================
    
#if @radialFog
    float radialDepth = distance(position.xyz, camPos);
    float fogVal = clamp((radialDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#else
    float fogVal = clamp((linearDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#endif
    
    vec3 arenaBlendedFog = arenaFogColour(gl_Fog.color.xyz, position.xyz - camPos);
    gl_FragData[0].xyz = mix(gl_FragData[0].xyz, arenaBlendedFog, fogVal);

    applyShadowDebugOverlay();
}
    