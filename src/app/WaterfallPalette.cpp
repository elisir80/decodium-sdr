// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/WaterfallPalette.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dsdr::app {

namespace {

struct ColorStop
{
    float position;
    float r, g, b;
};

/// Le palette, nell'ordine in cui la UI le elenca.
///
/// Tutte crescono in intensità percepita, ma non tutte in pura luminanza: nel
/// tratto finale il rosso acceso è più scuro dell'ambra che lo precede, e in
/// Turbo la scala è portata quasi per intero dalla tinta. È una convenzione che
/// vale la pena rispettare — sui ricevitori il rosso significa "forte" da
/// sempre. Quello che invece nessuna palette può fare è dare lo stesso colore a
/// livelli lontani: `tst_waterfall` verifica che due punti a un quarto di scala
/// di distanza restino distinguibili.
const std::vector<std::vector<ColorStop>> &palettes()
{
    static const std::vector<std::vector<ColorStop>> table = {
        // DECODIUM: nero → indaco → ciano → verde → ambra → rosso → bianco
        {
            {0.00f, 0.02f, 0.03f, 0.06f}, {0.18f, 0.05f, 0.09f, 0.35f},
            {0.36f, 0.05f, 0.42f, 0.62f}, {0.52f, 0.10f, 0.72f, 0.60f},
            {0.68f, 0.55f, 0.85f, 0.25f}, {0.82f, 0.98f, 0.72f, 0.15f},
            {0.93f, 1.00f, 0.35f, 0.15f}, {1.00f, 1.00f, 0.96f, 0.92f},
        },
        // Raptor: il blu → verde → giallo → rosso classico dei ricevitori
        {
            {0.00f, 0.00f, 0.00f, 0.00f}, {0.20f, 0.00f, 0.00f, 0.55f},
            {0.40f, 0.00f, 0.55f, 0.55f}, {0.60f, 0.00f, 0.75f, 0.00f},
            {0.80f, 0.95f, 0.95f, 0.00f}, {1.00f, 1.00f, 0.10f, 0.00f},
        },
        // Turbo: molte tinte distinte, per separare livelli vicini
        {
            {0.00f, 0.19f, 0.07f, 0.23f}, {0.14f, 0.24f, 0.36f, 0.81f},
            {0.29f, 0.10f, 0.72f, 0.85f}, {0.43f, 0.20f, 0.92f, 0.55f},
            {0.57f, 0.60f, 0.99f, 0.23f}, {0.71f, 0.94f, 0.85f, 0.15f},
            {0.86f, 0.99f, 0.48f, 0.09f}, {1.00f, 0.73f, 0.09f, 0.02f},
        },
        // Fuoco: nero → rosso → giallo → bianco
        {
            {0.00f, 0.00f, 0.00f, 0.00f}, {0.35f, 0.60f, 0.00f, 0.00f},
            {0.65f, 1.00f, 0.55f, 0.00f}, {0.88f, 1.00f, 0.95f, 0.30f},
            {1.00f, 1.00f, 1.00f, 1.00f},
        },
        // Scala di grigi: nessuna tinta a distrarre, utile in stampa e per chi
        // distingue male i colori
        {
            {0.00f, 0.00f, 0.00f, 0.00f}, {1.00f, 1.00f, 1.00f, 1.00f},
        },
    };
    return table;
}

} // namespace

QStringList waterfallPaletteNames()
{
    // I nomi restano quelli d'uso: chi arriva da un altro programma li
    // riconosce senza doverli provare tutti.
    return {QObject::tr("DECODIUM"), QObject::tr("Raptor"), QObject::tr("Turbo"),
            QObject::tr("Fuoco"), QObject::tr("Scala di grigi")};
}

QByteArray buildWaterfallColorMap(int paletteIndex)
{
    const auto &all = palettes();
    const auto &stops = all[static_cast<std::size_t>(
        std::clamp(paletteIndex, 0, static_cast<int>(all.size()) - 1))];

    QByteArray data;
    data.resize(kColorMapSize * 4);
    auto *out = reinterpret_cast<uchar *>(data.data());

    for (int i = 0; i < kColorMapSize; ++i) {
        const float t = static_cast<float>(i) / (kColorMapSize - 1);

        std::size_t segment = 0;
        while (segment + 2 < stops.size() && t > stops[segment + 1].position)
            ++segment;

        const ColorStop &a = stops[segment];
        const ColorStop &b = stops[segment + 1];
        const float span = std::max(b.position - a.position, 1e-6f);
        const float k = std::clamp((t - a.position) / span, 0.0f, 1.0f);

        out[i * 4 + 0] = static_cast<uchar>(std::lround(255.0f * (a.r + (b.r - a.r) * k)));
        out[i * 4 + 1] = static_cast<uchar>(std::lround(255.0f * (a.g + (b.g - a.g) * k)));
        out[i * 4 + 2] = static_cast<uchar>(std::lround(255.0f * (a.b + (b.b - a.b) * k)));
        out[i * 4 + 3] = 255;
    }
    return data;
}

} // namespace dsdr::app
