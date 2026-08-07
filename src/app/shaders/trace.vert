#version 440

layout(location = 0) in vec2 position;   // x: 0..1 sulla banda, y: 0..1 sul livello

layout(location = 0) out float v_level;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 color;
    float gradient;
    float viewStart;   // porzione di banda visibile, come per il waterfall
    float viewSpan;
} ubuf;

void main()
{
    v_level = position.y;

    // Zoom orizzontale: la traccia è costruita su tutta la banda, qui si
    // rimappa la sola porzione visibile. Farlo nel vertex shader evita di
    // ricostruire la geometria a ogni giro di rotellina.
    float x = (position.x - ubuf.viewStart) / max(ubuf.viewSpan, 1e-6);

    gl_Position = ubuf.mvp * vec4(x, position.y, 0.0, 1.0);
}
