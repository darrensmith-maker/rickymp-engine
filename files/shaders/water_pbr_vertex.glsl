#version 120

// ArenaMP/OpenMW 0.47 vertex bridge for the OpenMW 0.51 PBR water shader.
varying vec4 position;
varying float linearDepth;
varying vec3 worldPos;
varying vec2 rippleMapUV;
varying vec3 screenCoordsPassthrough;

uniform vec3 nodePosition;
uniform vec3 playerPos;

#include "shadows_vertex.glsl"

void main(void)
{
    vec4 modelPos = gl_Vertex;
    vec4 viewPos = gl_ModelViewMatrix * modelPos;
    gl_Position = gl_ModelViewProjectionMatrix * modelPos;

    position = modelPos;
    worldPos = modelPos.xyz + nodePosition.xyz;
    rippleMapUV = (worldPos.xy - playerPos.xy + (@rippleMapSize * @rippleMapWorldScale * 0.5))
        / (@rippleMapSize * @rippleMapWorldScale);

    // OpenMW 0.51 returns clip-space Z when reverse-Z is disabled, which is
    // exactly the legacy 0.47 behavior expected by its fog/shadow code.
    linearDepth = gl_Position.z;

    mat4 scaleBias = mat4(
        0.5,  0.0, 0.0, 0.0,
        0.0, -0.5, 0.0, 0.0,
        0.0,  0.0, 0.5, 0.0,
        0.5,  0.5, 0.5, 1.0);
    vec4 projected = scaleBias * gl_Position;
    screenCoordsPassthrough = projected.xyw;

    setupShadowCoords(viewPos, normalize((gl_NormalMatrix * gl_Normal).xyz));
}
