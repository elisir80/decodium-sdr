#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float rowOffset;       // riga di scrittura corrente, normalizzata 0..1
    float uMin;            // porzione visibile dello spettro (zoom orizzontale)
    float uMax;
    float blackThreshold;  // sotto questo livello resta fondo
    float gamma;           // <1 alza i segnali deboli, >1 li schiaccia
    float unused0;
    float unused1;
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

    // Soglia: quello che sta sotto è rumore e va lasciato al fondo, altrimenti
    // il waterfall è un tappeto colorato uniforme in cui non si distingue nulla.
    float usable = max(1.0 - ubuf.blackThreshold, 0.001);
    float adjusted = clamp((level - ubuf.blackThreshold) / usable, 0.0, 1.0);

    // Gamma: sotto 1 fa emergere i segnali deboli senza spostare la soglia,
    // che è la regolazione che serve quando la banda è quasi vuota.
    adjusted = pow(adjusted, max(ubuf.gamma, 0.01));

    vec3 rgb = texture(colorMapTex, vec2(adjusted, 0.5)).rgb;
    fragColor = vec4(rgb, 1.0);
}
