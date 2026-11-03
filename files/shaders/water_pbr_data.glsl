// Recommended to set in-game Water Texture Quality to High (if Refraction is disabled then set it to Low), and Water Reflections to Sky for the intended reflection balance.

// Set to 0 instead of 1 to disable different features
#define FOAM_ENABLED 1
#define WATER_SSS_ENABLED 1
#define MULTIPLE_WATER_TYPES 1
#define WATER_MULTICOLOR_ENABLED 1
#define CAUSTICS_ENABLED 0 // works only if in-game refractions are enabled
#define SUB_SURFACE_ABSORPTION_ENABLED 1 // works only if in-game refractions are enabled

// Can tweak each color channel   (Red,      Green,    Blue, );
const vec3 WATER_COLOR_1    = vec3(0.0,      0.04,     -0.015); // phytoplankton
const vec3 WATER_COLOR_2    = vec3(0.03,     0.04,     -0.01 ); // sediment
const vec3 WATER_SSS_COLOR  = vec3(0.02,     0.23,      0.25 ); // cyan

// For example, for a more stylized look:
// const vec3 WATER_COLOR_1  = vec3(0.085,    0.09,     -0.05 );
// const vec3 WATER_COLOR_2  = vec3(-0.07,    0.095,    0.08  );

// Different water types              (Red,     Green,  Blue,   Absorption);
const vec4 WATER_COLOR_SEA      = vec4(0.031,   0.092,  0.147,  0.00359600);
const vec4 WATER_COLOR_CALM     = vec4(0.039,   0.113,  0.107,  0.00229600);
const vec4 WATER_COLOR_SWAMP    = vec4(0.233,   0.211,  0.131,  0.00529600);
const vec4 WATER_COLOR_SULPHUR  = vec4(0.093,   0.131,  0.181,  0.00229600);
const vec4 WATER_COLOR_TROPICAL = vec4(0.200,   0.379,  0.424,  0.00269600);
const vec4 WATER_COLOR_INTERIOR = vec4(0.063,   0.131,  0.171,  0.00219600);

// Additional parameters for others      (  Wave Strength, Multicolor Intensity, Foam Intensity, Refraction Brightness);
const vec4 WATER_SEA_PARAMS_1 =      vec4(           1.00,                 1.00,           1.00,                  0.85);
const vec4 WATER_CALM_PARAMS_1 =     vec4(           0.65,                 0.50,           0.65,                  0.85);
const vec4 WATER_SWAMP_PARAMS_1 =    vec4(           0.65,                 0.00,           0.65,                  0.75);
const vec4 WATER_SULPHUR_PARAMS_1 =  vec4(           0.40,                 0.50,           0.65,                  0.65);
const vec4 WATER_TROPICAL_PARAMS_1 = vec4(           0.80,                 1.00,           1.00,                  0.90);
const vec4 WATER_INTERIOR_PARAMS_1 = vec4(           0.50,                 0.50,           0.00,                  0.85);

// Part 2 - parameters for others        ( SSS Intensity,    Shore sulphur, Caustic Intensity, Shore Distance Modifier);
const vec4 WATER_SEA_PARAMS_2 =      vec4(           1.0,              0.0,               0.5,                     0.0);
const vec4 WATER_CALM_PARAMS_2 =     vec4(           0.5,              0.0,               0.4,                     0.0);
const vec4 WATER_SWAMP_PARAMS_2 =    vec4(           0.0,              0.0,               0.4,                     0.0);
const vec4 WATER_SULPHUR_PARAMS_2 =  vec4(           0.2,              1.0,               0.5,                     0.0);
const vec4 WATER_TROPICAL_PARAMS_2 = vec4(           0.9,              0.0,               0.6,                     0.0);
const vec4 WATER_INTERIOR_PARAMS_2 = vec4(           0.0,              0.0,               0.6,                     0.0);

// Helper function
void UpdateWaterFactor(inout float factor, vec3 worldPos, vec4 boundsMinMax, vec2 center, vec2 halfSize, float blendDist)
{
    if (factor < 1.0
        && worldPos.x >= boundsMinMax.x && worldPos.x <= boundsMinMax.z
        && worldPos.y >= boundsMinMax.y && worldPos.y <= boundsMinMax.w)
    {
        vec2 d = abs(worldPos.xy - center) - halfSize;
        float sdf = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
        if (sdf < 0.0)
            factor = max(factor, smoothstep(0.0, blendDist, sdf));
    }
}

void UpdateWaterTypes(vec3 worldPos, bool isInterior, inout vec4 baseWaterColor,
    inout float waveStrength, inout float multicolorIntensity, inout float foamIntensity, inout float refractionBrightness,
    inout float sssIntensity, inout float shoreSulphurIntensity, inout float shoreDistanceModifier)
{
    // Early out if interior water
    if (isInterior)
    {
        baseWaterColor = WATER_COLOR_INTERIOR;
        waveStrength = WATER_INTERIOR_PARAMS_1.x;
        multicolorIntensity = WATER_INTERIOR_PARAMS_1.y;
        foamIntensity = WATER_INTERIOR_PARAMS_1.z;
        refractionBrightness = WATER_INTERIOR_PARAMS_1.w;
        sssIntensity = WATER_INTERIOR_PARAMS_2.x;
        shoreSulphurIntensity = WATER_INTERIOR_PARAMS_2.y;
        shoreDistanceModifier = WATER_INTERIOR_PARAMS_2.w;
        return;
    }

    // Initialize
    float calmFactor = 0.0;
    float swampFactor = 0.0;
    float sulphurFactor = 0.0;
    float tropicalFactor = 0.0;
    vec4 params1 = WATER_SEA_PARAMS_1;
    vec4 params2 = WATER_SEA_PARAMS_2;

    // Box surrounding the Vvardenfell boxes (update if you exceed these bounds)
    const vec4 vvBox = vec4(154000.0, 174500.0, -103000.0, -76000.0);
    if (worldPos.x <= vvBox.x && worldPos.x >= vvBox.z
        && worldPos.y <= vvBox.y && worldPos.y >= vvBox.w)
    {
        // Odai River
        const vec2 calmStart1 = vec2(-3500.0, 0.0); // one corner (whichever one you want)
        const vec2 calmEnd1 = vec2(-50000.0, -58000.0); // opposite corner (order doesn't matter)

        // S Vvardenfell
        const vec2 calmStart2 = vec2(136000.0, 5200.0);
        const vec2 calmEnd2 = vec2(-41000.0, -76000.0);

        // Gnisis & West Gash
        const vec2 calmStart3 = vec2(-54500.0, 125000.0);
        const vec2 calmEnd3 = vec2(-103000.0, 85000.0);

        // Azura's Coast
        const vec2 calmStart4 = vec2(154000.0, 69000.0);
        const vec2 calmEnd4 = vec2(82000.0, -67000.0);

        // Sheogorad
        const vec2 calmStart5 = vec2(78000.0, 174500.0);
        const vec2 calmEnd5 = vec2(-17000.0, 126000.0);

        // Gnaar Mok
        const vec2 swampStart = vec2(-30500.0, 56500.0);
        const vec2 swampEnd = vec2(-70500.0, -39000.0);

        // Lake Nabia
        const vec2 sulphurPos = vec2(43565.0, -16194.0);
        const float sulphurRadius = 15000.0;

        // Odai River
        const vec2 calmCenter1 = calmStart1 * 0.5 + calmEnd1 * 0.5;
        const vec2 calmHalfSize1 = abs(calmEnd1 - calmStart1) * 0.5;
        const vec4 calmBounds1 = vec4(
            min(calmStart1.x, calmEnd1.x),
            min(calmStart1.y, calmEnd1.y),
            max(calmStart1.x, calmEnd1.x),
            max(calmStart1.y, calmEnd1.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds1, calmCenter1, calmHalfSize1, -3000.0); // blending radius inside the box

        // S Vvardenfell
        const vec2 calmCenter2 = calmStart2 * 0.5 + calmEnd2 * 0.5;
        const vec2 calmHalfSize2 = abs(calmEnd2 - calmStart2) * 0.5;
        const vec4 calmBounds2 = vec4(
            min(calmStart2.x, calmEnd2.x),
            min(calmStart2.y, calmEnd2.y),
            max(calmStart2.x, calmEnd2.x),
            max(calmStart2.y, calmEnd2.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds2, calmCenter2, calmHalfSize2, -6000.0);

        // Gnisis & West Gash
        const vec2 calmCenter3 = calmStart3 * 0.5 + calmEnd3 * 0.5;
        const vec2 calmHalfSize3 = abs(calmEnd3 - calmStart3) * 0.5;
        const vec4 calmBounds3 = vec4(
            min(calmStart3.x, calmEnd3.x),
            min(calmStart3.y, calmEnd3.y),
            max(calmStart3.x, calmEnd3.x),
            max(calmStart3.y, calmEnd3.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds3, calmCenter3, calmHalfSize3, -2500.0);

        // Azura's Coast
        const vec2 calmCenter4 = calmStart4 * 0.5 + calmEnd4 * 0.5;
        const vec2 calmHalfSize4 = abs(calmEnd4 - calmStart4) * 0.5;
        const vec4 calmBounds4 = vec4(
            min(calmStart4.x, calmEnd4.x),
            min(calmStart4.y, calmEnd4.y),
            max(calmStart4.x, calmEnd4.x),
            max(calmStart4.y, calmEnd4.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds4, calmCenter4, calmHalfSize4, -3000.0);

        // Sheogorad
        const vec2 calmCenter5 = calmStart5 * 0.5 + calmEnd5 * 0.5;
        const vec2 calmHalfSize5 = abs(calmEnd5 - calmStart5) * 0.5;
        const vec4 calmBounds5 = vec4(
            min(calmStart5.x, calmEnd5.x),
            min(calmStart5.y, calmEnd5.y),
            max(calmStart5.x, calmEnd5.x),
            max(calmStart5.y, calmEnd5.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds5, calmCenter5, calmHalfSize5, -3000.0);

        // Gnaar Mok
        const vec2 swampCenter = swampStart * 0.5 + swampEnd * 0.5;
        const vec2 swampHalfSize = abs(swampEnd - swampStart) * 0.5;
        const vec4 swampBounds = vec4(
            min(swampStart.x, swampEnd.x),
            min(swampStart.y, swampEnd.y),
            max(swampStart.x, swampEnd.x),
            max(swampStart.y, swampEnd.y)
        );
        if (worldPos.x >= swampBounds.x && worldPos.x <= swampBounds.z
            && worldPos.y >= swampBounds.y && worldPos.y <= swampBounds.w)
        {
            vec2 d = abs(worldPos.xy - swampCenter) - swampHalfSize;
            float sdf = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
            if (sdf < 0.0)
            {
                swampFactor = smoothstep(0.0, -9000.0, sdf);

                const vec2 swampStartEx = vec2(-55000.0, -10500.0);
                const vec2 swampEndEx = vec2(-72000.0, -37200.0);
                const vec2 swampCenterEx = swampStartEx * 0.5 + swampEndEx * 0.5;
                const vec2 swampHalfSizeEx = abs(swampEndEx - swampStartEx) * 0.5;
                const vec4 swampBoundsEx = vec4(
                    min(swampStartEx.x, swampEndEx.x),
                    min(swampStartEx.y, swampEndEx.y),
                    max(swampStartEx.x, swampEndEx.x),
                    max(swampStartEx.y, swampEndEx.y)
                );
                if (worldPos.x >= swampBoundsEx.x && worldPos.x <= swampBoundsEx.z
                    && worldPos.y >= swampBoundsEx.y && worldPos.y <= swampBoundsEx.w)
                {
                    d = abs(worldPos.xy - swampCenterEx) - swampHalfSizeEx;
                    sdf = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
                    if (sdf < 0.0)
                        swampFactor = max(0.0, swampFactor - smoothstep(0.0, -5000.0, sdf));
                }

                const vec2 swampStartEx2 = vec2(-63000.0, -23000.0);
                const vec2 swampEndEx2 = vec2(-47500.0, -13000.0);
                const vec2 swampCenterEx2 = swampStartEx2 * 0.5 + swampEndEx2 * 0.5;
                const vec2 swampHalfSizeEx2 = abs(swampEndEx2 - swampStartEx2) * 0.5;
                const vec4 swampBoundsEx2 = vec4(
                    min(swampStartEx2.x, swampEndEx2.x),
                    min(swampStartEx2.y, swampEndEx2.y),
                    max(swampStartEx2.x, swampEndEx2.x),
                    max(swampStartEx2.y, swampEndEx2.y)
                );
                if (worldPos.x >= swampBoundsEx2.x && worldPos.x <= swampBoundsEx2.z
                    && worldPos.y >= swampBoundsEx2.y && worldPos.y <= swampBoundsEx2.w)
                {
                    d = abs(worldPos.xy - swampCenterEx2) - swampHalfSizeEx2;
                    sdf = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
                    if (sdf < 0.0)
                    {
                        swampFactor = max(0.0, swampFactor - smoothstep(0.0, -1000.0, sdf));
                        UpdateWaterFactor(calmFactor, worldPos, swampBoundsEx2, swampCenterEx2, swampHalfSizeEx2, -4000.0);
                    }
                }
            }
        }

        // Lake Nabia
        const vec4 sulphurBounds = vec4(
            sulphurPos.x - sulphurRadius,
            sulphurPos.y - sulphurRadius,
            sulphurPos.x + sulphurRadius,
            sulphurPos.y + sulphurRadius
        );
        if (worldPos.x >= sulphurBounds.x && worldPos.x <= sulphurBounds.z
            && worldPos.y >= sulphurBounds.y && worldPos.y <= sulphurBounds.w)
        {
            float sulphurDist = distance(worldPos.xy, sulphurPos);
            if (sulphurDist < sulphurRadius)
                sulphurFactor = smoothstep(sulphurRadius, 6200.0, sulphurDist);
        }
    }
    else
    {
        // Abecean Sea
        const vec2 tropicalStart = vec2(-920000.0, -140000.0);
        const vec2 tropicalEnd = vec2(-2090000.0, -1065000.0);

        // Mainland Tamriel
        const vec2 calmStart6 = vec2(221000.0, -145000.0);
        const vec2 calmEnd6 = vec2(-930000.0, -672000.0);

        // Skyrim
        const vec2 calmStart7 = vec2(-418000.0, 230000.0);
        const vec2 calmEnd7 = vec2(-1081500.0, -295000.0);

        // N Telvanni
        const vec2 calmStart8 = vec2(233500.0, 152000.0);
        const vec2 calmEnd8 = vec2(188000.0, 94500.0);

        // Tamriel East Coast
        const vec2 calmStart9 = vec2(310000.0, -10000.0);
        const vec2 calmEnd9 = vec2(190000.0, -270000.0);

        // C Telvanni
        const vec2 calmStart10 = vec2(291000.0, 116500.0);
        const vec2 calmEnd10 = vec2(208500.0, -20000.0);

        // Bodrum
        const vec2 calmStart11 = vec2(-53000.0, -104500.0);
        const vec2 calmEnd11 = vec2(-117000.0, -157000.0);

        // Necrom
        const vec2 calmStart12 = vec2(352000.0, -75000.0);
        const vec2 calmEnd12 = vec2(297000.0, -143000.0);

        // Andothren
        const vec2 calmStart13 = vec2(29500.0, -126600.0);
        const vec2 calmEnd13 = vec2(-13000.0, -159000.0);

        // Abecean Sea
        const vec2 tropicalCenter = tropicalStart * 0.5 + tropicalEnd * 0.5;
        const vec2 tropicalHalfSize = abs(tropicalEnd - tropicalStart) * 0.5;
        const vec4 tropicalBounds = vec4(
            min(tropicalStart.x, tropicalEnd.x),
            min(tropicalStart.y, tropicalEnd.y),
            max(tropicalStart.x, tropicalEnd.x),
            max(tropicalStart.y, tropicalEnd.y)
        );
        UpdateWaterFactor(tropicalFactor, worldPos, tropicalBounds, tropicalCenter, tropicalHalfSize, -10000.0);

        // Mainland Tamriel
        const vec2 calmCenter6 = calmStart6 * 0.5 + calmEnd6 * 0.5;
        const vec2 calmHalfSize6 = abs(calmEnd6 - calmStart6) * 0.5;
        const vec4 calmBounds6 = vec4(
            min(calmStart6.x, calmEnd6.x),
            min(calmStart6.y, calmEnd6.y),
            max(calmStart6.x, calmEnd6.x),
            max(calmStart6.y, calmEnd6.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds6, calmCenter6, calmHalfSize6, -10000.0);

        // Skyrim
        const vec2 calmCenter7 = calmStart7 * 0.5 + calmEnd7 * 0.5;
        const vec2 calmHalfSize7 = abs(calmEnd7 - calmStart7) * 0.5;
        const vec4 calmBounds7 = vec4(
            min(calmStart7.x, calmEnd7.x),
            min(calmStart7.y, calmEnd7.y),
            max(calmStart7.x, calmEnd7.x),
            max(calmStart7.y, calmEnd7.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds7, calmCenter7, calmHalfSize7, -5000.0);

        // N Telvanni
        const vec2 calmCenter8 = calmStart8 * 0.5 + calmEnd8 * 0.5;
        const vec2 calmHalfSize8 = abs(calmEnd8 - calmStart8) * 0.5;
        const vec4 calmBounds8 = vec4(
            min(calmStart8.x, calmEnd8.x),
            min(calmStart8.y, calmEnd8.y),
            max(calmStart8.x, calmEnd8.x),
            max(calmStart8.y, calmEnd8.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds8, calmCenter8, calmHalfSize8, -5000.0);

        // Tamriel East Coast
        const vec2 calmCenter9 = calmStart9 * 0.5 + calmEnd9 * 0.5;
        const vec2 calmHalfSize9 = abs(calmEnd9 - calmStart9) * 0.5;
        const vec4 calmBounds9 = vec4(
            min(calmStart9.x, calmEnd9.x),
            min(calmStart9.y, calmEnd9.y),
            max(calmStart9.x, calmEnd9.x),
            max(calmStart9.y, calmEnd9.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds9, calmCenter9, calmHalfSize9, -8000.0);

        // C Telvanni
        const vec2 calmCenter10 = calmStart10 * 0.5 + calmEnd10 * 0.5;
        const vec2 calmHalfSize10 = abs(calmEnd10 - calmStart10) * 0.5;
        const vec4 calmBounds10 = vec4(
            min(calmStart10.x, calmEnd10.x),
            min(calmStart10.y, calmEnd10.y),
            max(calmStart10.x, calmEnd10.x),
            max(calmStart10.y, calmEnd10.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds10, calmCenter10, calmHalfSize10, -9000.0);

        // Bodrum
        const vec2 calmCenter11 = calmStart11 * 0.5 + calmEnd11 * 0.5;
        const vec2 calmHalfSize11 = abs(calmEnd11 - calmStart11) * 0.5;
        const vec4 calmBounds11 = vec4(
            min(calmStart11.x, calmEnd11.x),
            min(calmStart11.y, calmEnd11.y),
            max(calmStart11.x, calmEnd11.x),
            max(calmStart11.y, calmEnd11.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds11, calmCenter11, calmHalfSize11, -7000.0);

        // Necrom
        const vec2 calmCenter12 = calmStart12 * 0.5 + calmEnd12 * 0.5;
        const vec2 calmHalfSize12 = abs(calmEnd12 - calmStart12) * 0.5;
        const vec4 calmBounds12 = vec4(
            min(calmStart12.x, calmEnd12.x),
            min(calmStart12.y, calmEnd12.y),
            max(calmStart12.x, calmEnd12.x),
            max(calmStart12.y, calmEnd12.y)
        );
        if (worldPos.x >= calmBounds12.x && worldPos.x <= calmBounds12.z
            && worldPos.y >= calmBounds12.y && worldPos.y <= calmBounds12.w)
        {
            vec2 d = abs(worldPos.xy - calmCenter12) - calmHalfSize12;
            float sdf = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
            if (sdf < 0.0)
            {
                calmFactor = smoothstep(0.0, -8000.0, sdf);

                const vec2 calmStartEx = vec2(353000.0, -114000.0);
                const vec2 calmEndEx = vec2(325000.0, -143000.0);
                const vec2 calmCenterEx = calmStartEx * 0.5 + calmEndEx * 0.5;
                const vec2 calmHalfSizeEx = abs(calmEndEx - calmStartEx) * 0.5;
                const vec4 calmBoundsEx = vec4(
                    min(calmStartEx.x, calmEndEx.x),
                    min(calmStartEx.y, calmEndEx.y),
                    max(calmStartEx.x, calmEndEx.x),
                    max(calmStartEx.y, calmEndEx.y)
                );
                if (worldPos.x >= calmBoundsEx.x && worldPos.x <= calmBoundsEx.z
                    && worldPos.y >= calmBoundsEx.y && worldPos.y <= calmBoundsEx.w)
                {
                    d = abs(worldPos.xy - calmCenterEx) - calmHalfSizeEx;
                    sdf = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
                    if (sdf < 0.0)
                        calmFactor = max(0.0, calmFactor - smoothstep(0.0, -1.0, sdf));
                }
            }
        }

        // Andothren
        const vec2 calmCenter13 = calmStart13 * 0.5 + calmEnd13 * 0.5;
        const vec2 calmHalfSize13 = abs(calmEnd13 - calmStart13) * 0.5;
        const vec4 calmBounds13 = vec4(
            min(calmStart13.x, calmEnd13.x),
            min(calmStart13.y, calmEnd13.y),
            max(calmStart13.x, calmEnd13.x),
            max(calmStart13.y, calmEnd13.y)
        );
        UpdateWaterFactor(calmFactor, worldPos, calmBounds13, calmCenter13, calmHalfSize13, -5000.0);
    }

    float totalFactor = calmFactor + swampFactor + tropicalFactor;
    if (totalFactor > 0.0)
    {
        float invTotal = 1.0 / totalFactor;
        vec4 specialWaterColor = WATER_COLOR_CALM * calmFactor
                                + WATER_COLOR_SWAMP * swampFactor
                                + WATER_COLOR_TROPICAL * tropicalFactor;
        params1 = WATER_CALM_PARAMS_1 * calmFactor
                     + WATER_SWAMP_PARAMS_1 * swampFactor
                     + WATER_TROPICAL_PARAMS_1 * tropicalFactor;
        params2 = WATER_CALM_PARAMS_2 * calmFactor
                     + WATER_SWAMP_PARAMS_2 * swampFactor
                     + WATER_TROPICAL_PARAMS_2 * tropicalFactor;

        totalFactor = clamp(totalFactor, 0.0, 1.0);
        baseWaterColor = mix(baseWaterColor, specialWaterColor * invTotal, totalFactor);
        params1 = mix(WATER_SEA_PARAMS_1, params1 * invTotal, totalFactor);
        params2 = mix(WATER_SEA_PARAMS_2, params2 * invTotal, totalFactor);
    }

    if (sulphurFactor > 0.0)
    {
        baseWaterColor = mix(baseWaterColor, WATER_COLOR_SULPHUR, sulphurFactor);

        params1 = mix(params1, WATER_SULPHUR_PARAMS_1, sulphurFactor);
        params2 = mix(params1, WATER_SULPHUR_PARAMS_2, sulphurFactor);
    }

    // Additional parameters for others      ( Wave Strength, Multicolor Intensity, Foam Intensity, Refraction Brightness)
    waveStrength = clamp(params1.x, 0.0f, 1.0f);
    multicolorIntensity = params1.y;
    foamIntensity = params1.z;
    refractionBrightness = params1.w;
    // Part 2 - parameters for others        ( SSS Intensity,    Shore sulphur, [unused], Shore Distance Modifier)
    sssIntensity = params2.x;
    shoreSulphurIntensity = params2.y;
    shoreDistanceModifier = params2.w;
}
// 2.0c
