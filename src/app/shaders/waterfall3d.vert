#version 440

// Vertice della griglia: x = posizione nella banda (0..1),
// y = età della riga (0 = più recente, 1 = più vecchia).
layout(location = 0) in vec2 grid;

layout(location = 0) out float v_level;
layout(location = 1) out float v_age;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out vec2 v_grid;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float rowOffset;     // riga di scrittura corrente, normalizzata
    float uMin;          // porzione di banda visibile (zoom)
    float uMax;
    float heightScale;   // quanto in alto sale un segnale pieno
    float depth;         // profondità della scena
    float floorLevel;    // livelli sotto questo restano piatti
    float gamma;         // <1 alza i segnali deboli, >1 li schiaccia
    float stepX;         // passo della griglia lungo la banda
    float stepZ;         // passo della griglia lungo il tempo
    float timeSpan;      // quanta storia entra nella profondità della scena
    float gridStrength;  // quanto marcato il reticolo di riferimento
} ubuf;

layout(binding = 1) uniform sampler2D waterfallTex;

/// Altezza della superficie in un punto qualunque della griglia.
///
/// Sotto la soglia resta piatta: senza, il rumore di fondo diventa un tappeto
/// ondulato che nasconde i segnali veri — e con l'illuminazione accesa quel
/// tappeto si accenderebbe anche di riflessi.
float reliefAt(vec2 g)
{
    // La texture è un anello: la riga più recente è quella appena scritta, e
    // si torna indietro nel tempo scendendo di riga.
    float row = fract(ubuf.rowOffset - g.y * ubuf.timeSpan);
    float u = mix(ubuf.uMin, ubuf.uMax, g.x);
    float level = texture(waterfallTex, vec2(u, row)).r;
    return max(level - ubuf.floorLevel, 0.0) / max(1.0 - ubuf.floorLevel, 0.001);
}

void main()
{
    float row = fract(ubuf.rowOffset - grid.y * ubuf.timeSpan);
    float u = mix(ubuf.uMin, ubuf.uMax, grid.x);

    // Campionare la texture nel vertex shader è ciò che rende possibile
    // costruire il rilievo senza rimandare i campioni alla CPU a ogni frame:
    // la storia sta già in memoria video.
    float level = texture(waterfallTex, vec2(u, row)).r;
    float relief = max(level - ubuf.floorLevel, 0.0) / max(1.0 - ubuf.floorLevel, 0.001);

    // ── Normale della superficie ─────────────────────────────────────────
    //
    // Dai quattro campioni vicini sulla griglia, non dai triangoli: una
    // normale per faccia darebbe una superficie sfaccettata, dove ogni
    // triangolo si legge come un piano a sé e le creste diventano scalini.
    // Presi sui vicini, i due vettori tangenti descrivono la stessa superficie
    // che il rasterizzatore disegna, e la normale interpolata fra i vertici
    // varia con continuità.
    //
    // I passi arrivano dal C++ perché la griglia la costruisce lui: lo shader
    // non ha modo di sapere quanti vertici ha attorno.
    float hLeft  = reliefAt(vec2(grid.x - ubuf.stepX, grid.y));
    float hRight = reliefAt(vec2(grid.x + ubuf.stepX, grid.y));
    float hNear  = reliefAt(vec2(grid.x, grid.y - ubuf.stepZ));
    float hFar   = reliefAt(vec2(grid.x, grid.y + ubuf.stepZ));

    // Le tangenti sono nelle stesse unità della scena, non della griglia:
    // altrimenti la pendenza cambierebbe con la profondità scelta e la luce
    // seguirebbe un parametro di inquadratura invece della forma del segnale.
    vec3 alongBand = vec3(2.0 * ubuf.stepX, (hRight - hLeft) * ubuf.heightScale, 0.0);
    vec3 alongTime = vec3(0.0, (hFar - hNear) * ubuf.heightScale, -2.0 * ubuf.stepZ * ubuf.depth);

    v_normal = normalize(cross(alongBand, alongTime));
    v_level = level;
    v_age = grid.y;
    v_grid = grid;

    // Scena in coordinate normalizzate: x attraversa la banda, y è
    // l'ampiezza, z è il tempo che si allontana.
    vec3 position = vec3(grid.x - 0.5,
                         relief * ubuf.heightScale,
                         -grid.y * ubuf.depth);

    gl_Position = ubuf.mvp * vec4(position, 1.0);
}
