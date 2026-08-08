#version 440

layout(location = 0) in float v_level;
layout(location = 1) in float v_age;

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
    float unused0;
} ubuf;

layout(binding = 2) uniform sampler2D colorMapTex;

void main()
{
    // Stessa tonalizzazione della vista piatta: cambiando modo di guardare
    // non deve cambiare il significato dei colori.
    float usable = max(1.0 - ubuf.floorLevel, 0.001);
    float adjusted = clamp((v_level - ubuf.floorLevel) / usable, 0.0, 1.0);
    adjusted = pow(adjusted, max(ubuf.gamma, 0.01));

    vec3 rgb = texture(colorMapTex, vec2(adjusted, 0.5)).rgb;

    // Le righe lontane sfumano verso il fondo: senza attenuazione la
    // prospettiva non si legge, perché il colore da solo non dice quale
    // parte della superficie sia vicina e quale lontana.
    float fade = 1.0 - v_age * 0.65;

    fragColor = vec4(rgb * fade, 1.0);
}
