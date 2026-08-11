#version 440

layout(location = 0) in float v_level;
layout(location = 1) in float v_age;
layout(location = 2) in vec3 v_normal;
layout(location = 3) in vec2 v_grid;

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
    float timeSpan;
    float gridStrength;
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

/// Quante divisioni ha il reticolo lungo la banda e lungo il tempo.
///
/// Otto e quattro perché devono restare contabili a colpo d'occhio: un
/// reticolo fitto, in prospettiva, si chiude su se stesso in fondo alla scena
/// e smette di essere un riferimento — diventa una tessitura.
const float kGridDivisionsX = 8.0;
const float kGridDivisionsZ = 4.0;

/// Quanto è marcata la riga del reticolo in questo punto: 1 sulla riga, 0 fra
/// una riga e l'altra.
///
/// Lo spessore si ricava dalla derivata a schermo della coordinata, non da una
/// costante: in prospettiva le celle vicine sono grandi e quelle lontane
/// minuscole, e una soglia fissa darebbe fasce spesse davanti e niente in
/// fondo — cioè proprio dove il riferimento serve di più.
float gridLine(float coord, float divisions)
{
    float scaled = coord * divisions;

    // Distanza dal bordo di cella più vicino, in unità di mezza cella: vale 1
    // esattamente sul bordo, dove sta la riga.
    float toEdge = abs(fract(scaled) - 0.5) * 2.0;

    // `fwidth` dà quanto cambia `scaled` da un pixel al successivo; `toEdge`
    // cambia il doppio, perché copre mezza cella in un'unità.
    float perPixel = max(fwidth(scaled), 1e-4) * 2.0;
    return smoothstep(1.0 - clamp(perPixel * 1.2, 0.01, 0.6), 1.0, toEdge);
}

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
    vec3 lit = clamp(rgb * shade * fade, 0.0, 1.0);

    // ── Reticolo ─────────────────────────────────────────────────────────
    //
    // In prospettiva la stessa distanza sullo schermo vale frequenze diverse a
    // seconda di quanto è lontana la riga: senza un reticolo su cui
    // appoggiarsi si vede *che* c'è un segnale, non *dove*. È la ragione per
    // cui il 3D, nella maggior parte dei programmi, resta un ornamento.
    //
    // Sta sopra la superficie e non sul fondo: seguendone il rilievo dice anche
    // quanto è alta una cresta rispetto al piano attorno.
    if (ubuf.gridStrength > 0.001) {
        float line = max(gridLine(v_grid.x, kGridDivisionsX),
                         gridLine(v_grid.y, kGridDivisionsZ));
        // Il reticolo si schiarisce dove la superficie è scura e si scurisce
        // dove è chiara: una riga di un colore solo sparirebbe proprio sui
        // segnali forti, che sono quelli che si vuole misurare.
        float luma = dot(lit, vec3(0.30, 0.59, 0.11));
        vec3 inkColor = luma > 0.45 ? vec3(0.0) : vec3(0.55, 0.72, 0.85);
        lit = mix(lit, inkColor, line * ubuf.gridStrength * 0.55 * fade);
    }

    fragColor = vec4(lit, 1.0);
}
