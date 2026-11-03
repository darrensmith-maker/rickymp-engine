#version 120

varying vec3 cloudRay;

void main()
{
    cloudRay = normalize(gl_Vertex.xyz);
    gl_Position = ftransform();
}
