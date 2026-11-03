// PBR compatibility layer for the TES3MP/OpenMW 0.47 lighting API.
// Keeps the existing shader interfaces while replacing legacy Blinn-Phong
// highlights with a normalized GGX/Smith/Schlick microfacet BRDF.
#ifndef TES3MP_PBR_COMPAT_GLSL
#define TES3MP_PBR_COMPAT_GLSL

const float PBR_PI = 3.14159265358979323846;

float pbrSaturate(float v) { return clamp(v, 0.0, 1.0); }
float pbrDistributionGGX(float NdotH, float roughness)
{
    float a = max(roughness * roughness, 0.0025);
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PBR_PI * d * d, 1e-5);
}
float pbrGeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-5);
}
float pbrGeometrySmith(float NdotV, float NdotL, float roughness)
{
    return pbrGeometrySchlickGGX(NdotV, roughness) * pbrGeometrySchlickGGX(NdotL, roughness);
}
vec3 pbrFresnelSchlick(float HdotV, vec3 f0)
{
    float f = pow(1.0 - pbrSaturate(HdotV), 5.0);
    return f0 + (1.0 - f0) * f;
}
float pbrRoughnessFromShininess(float shininess)
{
    return clamp(sqrt(2.0 / max(shininess + 2.0, 2.0)), 0.08, 1.0);
}

float pbrShininessFromRoughness(float roughness)
{
    float r = clamp(roughness, 0.08, 1.0);
    return clamp(2.0 / (r * r) - 2.0, 1.0, 255.0);
}

// Packed PBR parameter maps must be recognised very conservatively.  Legacy
// Morrowind specular/environment maps can have coloured RGB data, and a loose
// heuristic causes random rugs, wood, cloth and furniture to be interpreted as
// metal/roughness/AO textures, producing square or blotchy highlights.
bool pbrLooksLikePackedParameters(vec4 sampleValue)
{
    float spread = max(abs(sampleValue.r - sampleValue.g),
        max(abs(sampleValue.g - sampleValue.b), abs(sampleValue.r - sampleValue.b)));
    float maxChannel = max(sampleValue.r, max(sampleValue.g, sampleValue.b));
    float minChannel = min(sampleValue.r, min(sampleValue.g, sampleValue.b));

    // Reject near-greys and typical bright legacy spec maps.
    if (spread < 0.18)
        return false;
    if (maxChannel > 0.96 && minChannel > 0.72)
        return false;

    // Require a plausible packed-material layout: roughness and AO must both be
    // meaningfully present, and at least one of them should dominate metallic.
    bool plausibleRoughness = sampleValue.g > 0.16;
    bool plausibleAO = sampleValue.b > 0.10;
    bool plausibleBalance = (sampleValue.g > sampleValue.r + 0.04)
        || (sampleValue.b > sampleValue.r + 0.04)
        || sampleValue.r < 0.35;

    return plausibleRoughness && plausibleAO && plausibleBalance;
}

float pbrPackedRoughness(vec4 sampleValue)
{
    // Keep a safer floor for older converted maps.  The user-facing object/
    // terrain roughness sliders still shape the overall look, but this avoids
    // unstable micro-highlights on dense patterns such as rugs and wood grain.
    return clamp(sampleValue.g, 0.45, 1.0);
}

float pbrPackedAO(vec4 sampleValue)
{
    // Older maps sometimes leave AO empty. Empty must mean fully visible, not black.
    return sampleValue.b > 0.002 ? clamp(sampleValue.b, 0.18, 1.0) : 1.0;
}

vec3 pbrSafeTangentNormal(vec3 encodedNormal)
{
    vec3 n = encodedNormal * 2.0 - 1.0;
    float lengthSquared = dot(n, n);
    if (lengthSquared < 0.08)
        return vec3(0.0, 0.0, 1.0);

    n *= inversesqrt(lengthSquared);
    // Block invalid/back-facing texels and BC compression spikes from creating
    // isolated fireflies on silhouettes and atlas seams.
    n.z = max(n.z, 0.025);
    return normalize(n);
}

float pbrFilterRoughness(vec3 normalValue, float roughness)
{
    vec3 safeNormal = normalize(normalValue);

    // A material-independent floor keeps old Morrowind maps from behaving like
    // polished metal. Increase it slightly at grazing angles.
    float grazingSafety = 1.0 - clamp(abs(safeNormal.z), 0.0, 1.0);
    float filtered = max(roughness, 0.40 + grazingSafety * 0.12);

#ifdef ARENAMP_FRAGMENT_SHADER
    // Toksvig-style specular anti-aliasing. Normal variation inside one pixel
    // widens the GGX lobe instead of producing isolated white HDR fireflies.
    vec3 dNdx = dFdx(safeNormal);
    vec3 dNdy = dFdy(safeNormal);
    float normalVariance = max(dot(dNdx, dNdx), dot(dNdy, dNdy));
    float varianceRoughness = min(normalVariance * 0.40, 0.50);
    filtered = sqrt(filtered * filtered + varianceRoughness);
#endif

    return clamp(filtered, 0.40, 1.0);
}

vec3 pbrSunSpecular(vec3 N, vec3 V, vec3 L, float shininess, vec3 materialSpecular)
{
    N = normalize(N);
    V = normalize(V);
    L = normalize(L);
    vec3 H = normalize(V + L);
    float NdotL = pbrSaturate(dot(N, L));
    float NdotV = pbrSaturate(dot(N, V));
    float NdotH = pbrSaturate(dot(N, H));
    float HdotV = pbrSaturate(dot(H, V));
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);
    float roughness = pbrFilterRoughness(N, pbrRoughnessFromShininess(shininess));
    // This branch has no complete metalness workflow or reflection probes.
    // Keep dielectric F0 in a stable range so converted maps cannot create
    // mirror-bright ground, stone or wood.
    vec3 f0 = clamp(materialSpecular, vec3(0.004), vec3(0.028));
    float D = pbrDistributionGGX(NdotH, roughness);
    float G = pbrGeometrySmith(NdotV, NdotL, roughness);
    vec3 F = pbrFresnelSchlick(HdotV, f0);
    // This branch has no image-based lighting or reliable metalness data.
    // A full Schlick grazing response therefore reads as a pale reflective
    // coating on stone, cloth and terrain. Keep only a restrained direct-light
    // Fresnel rise while preserving the shape of the rough GGX highlight.
    F = min(F, f0 + vec3(0.025));
    vec3 result = (D * G * F / max(4.0 * NdotV * NdotL, 1e-4)) * NdotL;
    // There is no complete reflection environment in this engine branch. A
    // conservative energy cap avoids isolated HDR fireflies from direct sun.
    return min(result * 0.16, vec3(0.065));
}
#endif
