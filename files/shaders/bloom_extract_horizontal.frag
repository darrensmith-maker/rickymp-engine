#version 120

uniform sampler2D sceneTexture;
uniform vec2 inverseSceneSize;
uniform vec2 inverseBloomSize;
uniform float bloomThreshold;
uniform float bloomSoftKnee;
uniform float bloomRadius;

vec3 brightPart(vec3 color)
{
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float knee = max(bloomThreshold * bloomSoftKnee, 0.0001);
    float soft = clamp((luminance - bloomThreshold + knee) / (2.0 * knee), 0.0, 1.0);
    soft = soft * soft * (3.0 - 2.0 * soft);
    float contribution = max(luminance - bloomThreshold, 0.0) + soft * knee;
    return color * (contribution / max(luminance, 0.0001));
}

vec3 sampleBright(vec2 uv)
{
    return brightPart(max(texture2D(sceneTexture, clamp(uv, vec2(0.0), vec2(1.0))).rgb, vec3(0.0)));
}

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseBloomSize;
    vec2 stepUv = vec2(inverseSceneSize.x * max(bloomRadius, 0.5), 0.0);

    vec3 color = sampleBright(uv) * 0.22702703;
    color += sampleBright(uv + stepUv * 1.38461538) * 0.31621622;
    color += sampleBright(uv - stepUv * 1.38461538) * 0.31621622;
    color += sampleBright(uv + stepUv * 3.23076923) * 0.07027027;
    color += sampleBright(uv - stepUv * 3.23076923) * 0.07027027;

    gl_FragColor = vec4(color, 1.0);
}
