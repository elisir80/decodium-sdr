#version 440

// Vertice della griglia: x = posizione nella banda (0..1),
// y = età della riga (0 = più recente, 1 = più vecchia).
layout(location = 0) in vec2 grid;

layout(location = 0) out float v_level;
layout(location = 1) out float v_age;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float rowOffset;     // riga di scrittura corrente, normalizzata
    float uMin;          // porzione di banda visibile (zoom)
    float uMax;
    float heightScale;   // quanto in alto sale un segnale pieno
    float depth;         // profondità della scena
    float floorLevel;    // livelli sotto questo restano piatti
    float gamma;         // <1 alza i segnali deboli, >1 li schiaccia
    float unused0;
} ubuf;

layout(binding = 1) uniform sampler2D waterfallTex;

void main()
{
    // La texture è un anello: la riga più recente è quella appena scritta, e
    // si torna indietro nel tempo scendendo di riga.
    float row = fract(ubuf.rowOffset - grid.y);
    float u = mix(ubuf.uMin, ubuf.uMax, grid.x);

    // Campionare la texture nel vertex shader è ciò che rende possibile
    // costruire il rilievo senza rimandare i campioni alla CPU a ogni frame:
    // la storia sta già in memoria video.
    float level = texture(waterfallTex, vec2(u, row)).r;

    // Sotto la soglia la superficie resta piatta: senza, il rumore di fondo
    // diventa un tappeto ondulato che nasconde i segnali veri.
    float relief = max(level - ubuf.floorLevel, 0.0) / max(1.0 - ubuf.floorLevel, 0.001);

    v_level = level;
    v_age = grid.y;

    // Scena in coordinate normalizzate: x attraversa la banda, y è
    // l'ampiezza, z è il tempo che si allontana.
    vec3 position = vec3(grid.x - 0.5,
                         relief * ubuf.heightScale,
                         -grid.y * ubuf.depth);

    gl_Position = ubuf.mvp * vec4(position, 1.0);
}
