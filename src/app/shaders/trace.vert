#version 440

layout(location = 0) in vec2 position;   // x: 0..1 sulla banda, y: 0..1 sul livello

layout(location = 0) out float v_level;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 color;
    float gradient;
} ubuf;

void main()
{
    v_level = position.y;
    gl_Position = ubuf.mvp * vec4(position, 0.0, 1.0);
}
