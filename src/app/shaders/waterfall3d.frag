#version 440

layout(location = 0) in float v_level;
layout(location = 1) in float v_age;
layout(location = 2) in vec3 v_normal;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    float rowOffset;
    float uMin;
    float uMax;
    float heightScale;
    float depth;
    float floorLevel;
    float gamma;
    float stepX;
    float stepZ;
} ubuf;

layout(binding = 2) uniform sampler2D colorMapTex;

/// Direzione della luce, nello spazio della superficie e non della scena.
///
/// Fissa rispetto ai dati: ruotando la vista le ombre non si spostano, e la
/// stessa cresta si legge sempre allo stesso modo. È la convenzione delle
/// carte in rilievo, ed è quella giusta qui — la luce serve a far vedere la
/// forma del segnale, non a simulare una stanza.
///
/// Radente per scelta: con la luce alta un rilievo di pochi decibel non
/// proietta niente, e la superficie torna piatta come prima. A poco più di
/// venticinque gradi sopra l'orizzonte anche una gobba minima ha un fianco in
/// luce e uno in ombra.
const vec3 kLightDir = normalize(vec3(-0.60, 0.46, 0.66));

/// Quanto la luce può scostarsi dal colore della palette.
///
/// A zero il rilievo sarebbe di nuovo solo geometria; a uno la superficie
/// diventerebbe un modello illuminato in cui la tinta non dice più il livello.
const float kLightStrength = 0.65;

void main()
{
    // Stessa tonalizzazione della vista piatta: cambiando modo di guardare
    // non deve cambiare il significato dei colori.
    float usable = max(1.0 - ubuf.floorLevel, 0.001);
    float adjusted = clamp((v_level - ubuf.floorLevel) / usable, 0.0, 1.0);
    adjusted = pow(adjusted, max(ubuf.gamma, 0.01));

    vec3 rgb = texture(colorMapTex, vec2(adjusted, 0.5)).rgb;

    // Il piano orizzontale vale esattamente uno. Non è un dettaglio: sul fondo
    // di rumore, che è piatto e occupa quasi tutta la superficie, il colore
    // resta quello che la palette assegna a quel livello — la luce aggiunge
    // volume dove c'è una forma e sparisce dove non ce n'è. Un Lambert crudo
    // avrebbe invece scurito tutto il piano di una quantità costante, cioè
    // avrebbe spostato il significato dei colori senza dirlo.
    vec3 n = normalize(v_normal);
    float lambert = max(dot(n, kLightDir), 0.0);
    float onTheFlat = max(kLightDir.y, 0.001);
    float shade = mix(1.0, lambert / onTheFlat, kLightStrength);

    // Gli estremi restano leggibili: un fianco in ombra non diventa il fondo,
    // uno in luce non brucia fino a perdere la tinta.
    shade = clamp(shade, 0.30, 1.45);

    // Le righe lontane sfumano verso il fondo: senza attenuazione la
    // prospettiva non si legge, perché il colore da solo non dice quale
    // parte della superficie sia vicina e quale lontana.
    float fade = 1.0 - v_age * 0.65;

    fragColor = vec4(clamp(rgb * shade * fade, 0.0, 1.0), 1.0);
}
