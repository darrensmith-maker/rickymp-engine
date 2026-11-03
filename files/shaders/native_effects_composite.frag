#version 120

uniform sampler2D sceneTexture;
uniform sampler2D depthTexture;
uniform vec2 inverseSceneSize;
uniform mat4 inverseProjectionMatrix;
uniform mat4 projectionMatrix;
uniform float ssrEnabled;
uniform float ssrStrength;
uniform float ssrDistance;

vec3 reconstructViewPosition(vec2 uv, float depth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = inverseProjectionMatrix * clip;
    return view.xyz / max(abs(view.w), 1e-6) * sign(view.w);
}

vec2 projectViewPosition(vec3 p)
{
    vec4 clip = projectionMatrix * vec4(p, 1.0);
    if (abs(clip.w) < 1e-5)
        return vec2(-1.0);
    return clip.xy / clip.w * 0.5 + 0.5;
}

vec3 reconstructNormal(vec2 uv, vec3 p)
{
    vec2 dx = vec2(inverseSceneSize.x, 0.0);
    vec2 dy = vec2(0.0, inverseSceneSize.y);
    vec3 px = reconstructViewPosition(clamp(uv + dx, vec2(0.0), vec2(1.0)),
        texture2D(depthTexture, clamp(uv + dx, vec2(0.0), vec2(1.0))).r);
    vec3 py = reconstructViewPosition(clamp(uv + dy, vec2(0.0), vec2(1.0)),
        texture2D(depthTexture, clamp(uv + dy, vec2(0.0), vec2(1.0))).r);
    vec3 n = normalize(cross(px - p, py - p));
    if (dot(n, -p) < 0.0)
        n = -n;
    return n;
}

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseSceneSize;
    vec3 color = texture2D(sceneTexture, uv).rgb;
    float depth = texture2D(depthTexture, uv).r;

    if (ssrEnabled >= 0.5 && depth < 0.9998)
    {
        vec3 viewPos = reconstructViewPosition(uv, depth);
        vec3 N = reconstructNormal(uv, viewPos);
        vec3 I = normalize(viewPos);
        vec3 R = normalize(reflect(I, N));
        float nDotV = max(dot(N, -I), 0.0);
        float grazing = pow(1.0 - nDotV, 2.2);
        float maxDistance = max(ssrDistance, 64.0);
        float stepLength = maxDistance / 18.0;
        vec3 hitColor = vec3(0.0);
        float hit = 0.0;

        for (int i = 1; i <= 18; ++i)
        {
            vec3 rayPos = viewPos + R * (stepLength * float(i));
            vec2 hitUv = projectViewPosition(rayPos);
            if (hitUv.x <= 0.002 || hitUv.y <= 0.002 || hitUv.x >= 0.998 || hitUv.y >= 0.998)
                break;
            float sampleDepth = texture2D(depthTexture, hitUv).r;
            if (sampleDepth >= 0.99995)
                continue;
            vec3 scenePos = reconstructViewPosition(hitUv, sampleDepth);
            float thickness = 18.0 + length(rayPos) * 0.0025;
            if (abs(scenePos.z - rayPos.z) < thickness)
            {
                hitColor = texture2D(sceneTexture, hitUv).rgb;
                float edge = min(min(hitUv.x, hitUv.y), min(1.0-hitUv.x, 1.0-hitUv.y));
                hit = smoothstep(0.0, 0.08, edge);
                break;
            }
        }

        float reflectivity = clamp(ssrStrength, 0.0, 1.0) * hit
            * clamp(0.08 + grazing * 0.92, 0.0, 1.0);
        color = mix(color, hitColor, reflectivity);
    }

    gl_FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
