#version 120

// Интеграция файла с функциями волн и каустики
#include "water_waves.glsl"
    
varying vec3  screenCoordsPassthrough;
varying vec4  position;
varying float linearDepth;
uniform float osg_SimulationTime;
uniform mat4 osg_ViewMatrixInverse;

#include "shadows_vertex.glsl"

void main(void)
{
    vec4 glvertice = gl_Vertex;
    
    vec4 campos = osg_ViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0);
    vec4 viewPos = gl_ModelViewMatrix * gl_Vertex;
    float euclideanDepth = length(viewPos.xyz);
    
    float frequency = 2.0*3.1415/0.1;

    // ========================================================================
    // УЛУЧШЕНИЕ: Скорость волн уменьшена в 2 раза (0.02 → 0.01)
    // ========================================================================
    float phase = 0.01 * frequency;  // было 0.02
    
    
    // ========================================================================
    // УЛУЧШЕНИЕ 2: Убрано волнение по вертикали в интерьере
    // Проверяем, находится ли камера под водой (интерьер)
    // Волны по вертикали применяются только в экстерьере
    // ========================================================================
    
    bool isInterior = (campos.z < -1.0);
    
    if(euclideanDepth < 600000.0 && !isInterior) {
    
        glvertice.xy *= 0.03;

        float theta = dot(vec2(0.6,0.4), vec2(glvertice.xy));
        float sinres = sin(theta * frequency + osg_SimulationTime * phase);
        float h = pow((sinres + 1.0) * 0.5, 2.5);
        
        float g = 70.0 * 9.8;
        
        // ========================================================================
        // УЛУЧШЕНИЕ 3: Амплитуда уменьшена в 2 раза (12.5 → 6.25)
        // Верхняя точка - это реальный уровень воды, волны идут только вниз
        // ========================================================================
        glvertice.z -= 6.25/sqrt(g) * sqrt(h * g);  // было 12.5
    }
    
    if(campos.z < -1.0)
        glvertice.z += 3.25;  // было 12.5, уменьшено в 2 раза для консистентности
    
    
    viewPos = gl_ModelViewMatrix * glvertice;
    gl_Position = gl_ModelViewProjectionMatrix * glvertice;

    mat4 scalemat = mat4(0.5, 0.0, 0.0, 0.0,
                         0.0, -0.5, 0.0, 0.0,
                         0.0, 0.0, 0.5, 0.0,
                         0.5, 0.5, 0.5, 1.0);

    vec4 texcoordProj = ((scalemat) * ( gl_Position));
    screenCoordsPassthrough = texcoordProj.xyw -
        vec3(0.0,0.0,0.0);

    position = glvertice;

    linearDepth = gl_Position.z;

#if (@shadows_enabled)
    vec3 viewNormal = normalize((gl_NormalMatrix * gl_Normal).xyz);
    setupShadowCoords(viewPos, viewNormal);
#endif
}
