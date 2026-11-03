#version 120

uniform sampler2D bloomTexture;
uniform vec2 inverseBloomSize;
uniform float bloomRadius;

vec3 bloomSample(vec2 uv)
{
    return max(texture2D(bloomTexture, clamp(uv, vec2(0.0), vec2(1.0))).rgb, vec3(0.0));
}

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseBloomSize;
    vec2 stepUv = vec2(0.0, inverseBloomSize.y * max(bloomRadius * 0.5, 0.5));

    vec3 color = bloomSample(uv) * 0.22702703;
    color += bloomSample(uv + stepUv * 1.38461538) * 0.31621622;
    color += bloomSample(uv - stepUv * 1.38461538) * 0.31621622;
    color += bloomSample(uv + stepUv * 3.23076923) * 0.07027027;
    color += bloomSample(uv - stepUv * 3.23076923) * 0.07027027;

    gl_FragColor = vec4(color, 1.0);
}
