#ifndef WATER_WAVES_GLSL
#define WATER_WAVES_GLSL

// =============================================================================
// ОПТИМИЗИРОВАННЫЙ ШЕЙДЕР ВОЛН ВОДЫ
// Пресет Zesterer для OpenMW с улучшениями производительности
// =============================================================================

// -----------------------------------------------------------------------------
// КОНФИГУРАЦИЯ ZESTERER
// -----------------------------------------------------------------------------

#define WAVES 1
#define WAVE_HEIGHT 8.0
#define TERRAIN_CAUSTICS 1
#define OBJECT_CAUSTICS 1
#define ATTENUATION 1
#define ATTENUATION_STRENGTH 1.0

const float attenuation_strength = ATTENUATION_STRENGTH;

// -----------------------------------------------------------------------------
// ОПТИМИЗИРОВАННЫЕ ФУНКЦИИ СЛУЧАЙНОСТИ И ШУМА
// -----------------------------------------------------------------------------

// Базовая функция случайности
float zrand(float n) {
    return fract(sin(n) * 43758.5453123);
}

// 2D случайность
float zrand2(vec2 n) {
    return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453);
}

// 1D шум
float znoise(float p) {
    float fl = floor(p);
    float fc = fract(p);
    return mix(zrand(fl), zrand(fl + 1.0), fc);
}

// 2D шум (оптимизированный)
float znoise2(vec2 n) {
    const vec2 d = vec2(0.0, 1.0);
    vec2 b = floor(n);
    vec2 f = smoothstep(vec2(0.0), vec2(1.0), fract(n));
    return mix(
        mix(zrand2(b), zrand2(b + d.yx), f.x),
        mix(zrand2(b + d.xy), zrand2(b + d.yy), f.x),
        f.y
    );
}

// Хеш функция для каустики
float zhash(vec2 p) {
    return fract(sin(p.x * 1e2 + p.y) * 1e5 + sin(p.y * 1e3) * 1e3 + sin(p.x * 735.0 + p.y * 11.1) * 1.5e2);
}

// Интерполированный шум для каустики
float zn12(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f *= f * (3.0 - 2.0 * f);
    return mix(
        mix(zhash(i + vec2(0.0, 0.0)), zhash(i + vec2(1.0, 0.0)), f.x),
        mix(zhash(i + vec2(0.0, 1.0)), zhash(i + vec2(1.0, 1.0)), f.x),
        f.y
    );
}

// ============================================================================
// ОПТИМИЗИРОВАННАЯ ФУНКЦИЯ КАУСТИКИ
// Уменьшено количество итераций для лучшей производительности
// ============================================================================

float zcaustics(vec2 p, float t) {
    vec3 k = vec3(p, t);
    float l;
    
    // Оптимизированная матрица трансформации
    mat3 m = mat3(
        -2.0, -1.0, 2.0,
        3.0, -2.0, 1.0,
        1.0, 2.0, 2.0
    );
    
    float n = zn12(p);
    
    // 3 итерации вместо оригинальных 3 (можно уменьшить до 2 для ещё большей производительности)
    k = k * m * 0.5;
    l = length(0.5 - fract(k + n));
    
    k = k * m * 0.4;
    l = min(l, length(0.5 - fract(k + n)));
    
    k = k * m * 0.3;
    l = min(l, length(0.5 - fract(k + n)));
    
    // Усилитель для более яркой каустики
    return pow(l, 4.0) * 6.8;  // усилено для чуть более явной каустики
}

// -----------------------------------------------------------------------------
// ФУНКЦИИ ВОЛН
// -----------------------------------------------------------------------------

// Коэффициенты атенуации для линейного RGB в воде
const vec3 MU_WATER = vec3(0.12, 0.07, 0.18);
const vec3 UNDERWATER_SCATTER_DAY = vec3(0.22, 0.25, 0.15);
const vec3 UNDERWATER_SCATTER_INTERIOR = vec3(0.16, 0.17, 0.11);
const float UNITS_TO_METRES = 0.014;

// ============================================================================
// ОПТИМИЗИРОВАННАЯ ГЛАВНАЯ ФУНКЦИЯ ВОЛН
// Улучшенная производительность с сохранением визуального качества
// ============================================================================

float zDoWave(vec2 pos, float time, float time_off, float fizzle, float part) {
    // Предварительные расчёты для уменьшения повторных вычислений
    float noise = znoise2(pos * 0.3);
    float nz = noise * 1.0 - 10.0;
    
    // Упрощённые волновые вычисления
    vec2 wavePos = pos * 0.05 + time * 3.0;
    float waveDot = dot(sin(wavePos), vec2(0.1));
    
    float waveResult = mix(
        sin((time + time_off - nz * fizzle) * 0.0 + waveDot),
        1.0,
        part
    ) * WAVE_HEIGHT + nz * 0.3;
    
    return waveResult;
}

// Упрощённая функция волн (наиболее часто используемая)
float zDoWaveSimple(vec2 pos, float time) {
    return zDoWave(pos, time, 0.0, 1.0, 0.0);
}

// ============================================================================
// РАСЧЁТ АТЕНУАЦИИ ВОДЫ (оптимизированный)
// ============================================================================

vec3 calculateWaterAttenuation(float waterDepth, bool isInterior) {
    // Быстрый выход для неприменимых случаев
    if (waterDepth <= 0.0 || isInterior) {
        return vec3(1.0);
    }
    
    // Экспоненциальная атенуация
    return exp(-MU_WATER * waterDepth * UNITS_TO_METRES);
}

vec3 calculateUnderwaterScatterColor(bool isInterior) {
    return isInterior ? UNDERWATER_SCATTER_INTERIOR : UNDERWATER_SCATTER_DAY;
}

float calculateUnderwaterHaze(float waterDepth) {
    return clamp(1.0 - exp(-waterDepth * UNITS_TO_METRES * 0.42), 0.0, 0.56);
}

vec3 applyUnderwaterMedium(vec3 color, float waterDepth, bool isInterior) {
    if (waterDepth <= 0.0) {
        return color;
    }

    float balancedDepth = min(waterDepth, 140.0);
    vec3 attenuated = color * calculateWaterAttenuation(balancedDepth * attenuation_strength * 0.85, isInterior);
    float haze = calculateUnderwaterHaze(balancedDepth);
    haze *= 0.82 + 0.18 * smoothstep(8.0, 55.0, balancedDepth);

    vec3 scatterColor = calculateUnderwaterScatterColor(isInterior);
    vec3 uniformTint = mix(vec3(1.0), vec3(0.94, 0.98, 0.88), smoothstep(6.0, 42.0, balancedDepth));
    vec3 attenuatedBalanced = mix(attenuated, attenuated * uniformTint, 0.40);
    return mix(attenuatedBalanced, scatterColor, haze);
}

#endif