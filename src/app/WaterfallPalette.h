// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — tabelle di colore del waterfall.
//
// Sta fuori dal renderer perché è l'unico pezzo di quella resa che si può
// verificare senza una GPU: la regola che le rende leggibili — luminosità
// sempre crescente col livello — è una proprietà della tabella, non del
// disegno.
#pragma once

#include <QByteArray>
#include <QStringList>

namespace dsdr::app {

/// Numero di livelli della tabella caricata come texture 1D.
inline constexpr int kColorMapSize = 256;

/// Quota della scala oltre la quale il passaggio dal freddo al caldo arriva
/// troppo tardi per il traffico che si guarda davvero.
///
/// Il perché sta in `WaterfallPalette.cpp`, accanto alle tabelle: qui il valore
/// è esposto perché è un contratto verificabile, e `tst_waterfall` lo verifica.
inline constexpr float kWarmBreakLimit = 0.60f;

/// Nomi delle palette, nell'ordine degli indici.
QStringList waterfallPaletteNames();

/// Tabella RGBA8 di `kColorMapSize` voci per la palette indicata. Gli indici
/// fuori intervallo ricadono sulla prima palette.
QByteArray buildWaterfallColorMap(int paletteIndex);

} // namespace dsdr::app
