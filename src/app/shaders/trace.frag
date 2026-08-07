#version 440

layout(location = 0) in float v_level;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 color;
    float gradient;   // 1 per il riempimento sotto la traccia, 0 per la linea
} ubuf;

void main()
{
    float alpha = ubuf.color.a * mix(1.0, 0.08 + 0.55 * v_level, ubuf.gradient);
    // La scene graph di Qt Quick lavora in alpha pre-moltiplicato.
    fragColor = vec4(ubuf.color.rgb * alpha, alpha);
}
