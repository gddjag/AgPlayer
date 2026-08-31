#version 440

layout(location = 0) in vec2 vertexPosition;
layout(location = 1) in float alphaRole;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 baseColor;
    vec4 alphas;
    vec4 progress;
} ubuf;

layout(location = 0) out float localX;
layout(location = 1) out float vertexAlphaRole;

void main()
{
    localX = vertexPosition.x;
    vertexAlphaRole = alphaRole;
    gl_Position = ubuf.qt_Matrix * vec4(vertexPosition, 0.0, 1.0);
}
