#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float rowOffset;   // riga di scrittura corrente, normalizzata 0..1
    float uMin;        // porzione visibile dello spettro (zoom orizzontale)
    float uMax;
    float unused;
} ubuf;

layout(binding = 1) uniform sampler2D waterfallTex;  // R8: livello normalizzato
layout(binding = 2) uniform sampler2D colorMapTex;   // 256×1 RGBA8

void main()
{
    // La texture è un anello: si scrive una riga per frame e si fa scorrere
    // l'offset UV, invece di ridisegnare tutta la storia a ogni fotogramma.
    float age = 1.0 - v_uv.y;                 // 0 in alto (riga più recente)
    float row = fract(ubuf.rowOffset - age);

    float u = mix(ubuf.uMin, ubuf.uMax, v_uv.x);
    float level = texture(waterfallTex, vec2(u, row)).r;

    vec3 rgb = texture(colorMapTex, vec2(level, 0.5)).rgb;
    fragColor = vec4(rgb, 1.0);
}
