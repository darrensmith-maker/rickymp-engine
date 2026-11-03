#version 120

uniform sampler2D bloomTexture;
uniform vec2 inverseSceneSize;
uniform float bloomIntensity;

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseSceneSize;
    vec3 bloom = texture2D(bloomTexture, clamp(uv, vec2(0.0), vec2(1.0))).rgb;

    // The pass is blended with GL_ONE, GL_ONE over the scene that is already in
    // the window framebuffer. It never replaces the scene, preventing a failed
    // Bloom capture from producing a black screen.
    gl_FragColor = vec4(max(bloom, vec3(0.0)) * max(bloomIntensity, 0.0), 0.0);
}
