#version 120
uniform sampler2D sceneTexture;
uniform sampler2D depthTexture;
uniform vec2 inverseSceneSize;
uniform float smaaThreshold;

float colorDistance(vec3 a, vec3 b)
{
    float meanR = 0.5 * (a.r + b.r);
    vec3 d = a - b;
    return sqrt((2.0 + meanR) * d.r * d.r + 4.0 * d.g * d.g + (3.0 - meanR) * d.b * d.b) * 0.36;
}

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseSceneSize;
    vec2 dx = vec2(inverseSceneSize.x, 0.0);
    vec2 dy = vec2(0.0, inverseSceneSize.y);
    vec2 lo = vec2(0.0);
    vec2 hi = vec2(1.0);

    vec3 c = texture2D(sceneTexture, uv).rgb;
    vec3 l = texture2D(sceneTexture, clamp(uv - dx, lo, hi)).rgb;
    vec3 r = texture2D(sceneTexture, clamp(uv + dx, lo, hi)).rgb;
    vec3 b = texture2D(sceneTexture, clamp(uv - dy, lo, hi)).rgb;
    vec3 t = texture2D(sceneTexture, clamp(uv + dy, lo, hi)).rgb;

    float d = texture2D(depthTexture, uv).r;
    float dl = texture2D(depthTexture, clamp(uv - dx, lo, hi)).r;
    float dr = texture2D(depthTexture, clamp(uv + dx, lo, hi)).r;
    float db = texture2D(depthTexture, clamp(uv - dy, lo, hi)).r;
    float dt = texture2D(depthTexture, clamp(uv + dy, lo, hi)).r;

    float threshold = clamp(smaaThreshold, 0.02, 0.35);
    float leftDelta = max(colorDistance(c, l), abs(d - dl) * 22.0);
    float rightDelta = max(colorDistance(c, r), abs(d - dr) * 22.0);
    float bottomDelta = max(colorDistance(c, b), abs(d - db) * 22.0);
    float topDelta = max(colorDistance(c, t), abs(d - dt) * 22.0);

    // Local contrast adaptation suppresses weak edges next to a much stronger
    // edge, which avoids over-blurring texture detail while keeping geometry.
    float maxHorizontal = max(leftDelta, rightDelta);
    float maxVertical = max(bottomDelta, topDelta);
    float ex = step(threshold, leftDelta) * step(maxHorizontal * 0.50, leftDelta);
    float ey = step(threshold, bottomDelta) * step(maxVertical * 0.50, bottomDelta);

    gl_FragColor = vec4(ex, ey, 0.0, 1.0);
}
