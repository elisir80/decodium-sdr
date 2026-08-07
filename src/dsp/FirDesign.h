// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — progetto di filtri FIR (finestra di Kaiser).
//
// Implementazione originale dal metodo windowed-sinc descritto in Lyons,
// "Understanding Digital Signal Processing", cap. 5 (§5 della spec).
#pragma once

#include "dsp/DspTypes.h"

#include <vector>

namespace dsdr::dsp {

/// Funzione di Bessel modificata di prima specie, ordine 0 (serie troncata).
double besselI0(double x);

/// Numero di tap necessario per una transizione `transitionHz` con
/// attenuazione `stopbandDb`, secondo la stima empirica di Kaiser.
/// Il risultato è sempre dispari (fase lineare simmetrica).
int estimateTaps(double transitionHz, double sampleRate, double stopbandDb = 80.0);

/// Beta di Kaiser corrispondente all'attenuazione richiesta.
double kaiserBeta(double stopbandDb);

/// Passa-basso reale a fase lineare, guadagno unitario in banda passante.
std::vector<float> designLowpass(double cutoffHz,
                                 double sampleRate,
                                 int numTaps,
                                 double beta = 8.0);

/// Passa-banda complesso: passa-basso di semilarghezza (hi-lo)/2 traslato al
/// centro (hi+lo)/2. Con `lo` e `hi` di segno concorde seleziona una singola
/// banda laterale — è così che si ottiene l'SSB senza trasformata di Hilbert.
std::vector<Complex> designBandpass(double loHz,
                                    double hiHz,
                                    double sampleRate,
                                    int numTaps,
                                    double beta = 8.0);

/// Variante che scrive nel vettore fornito senza riallocare, purché la
/// capacità sia sufficiente. Usata quando il filtro cambia a runtime
/// (RNF-05: niente allocazioni nel percorso caldo).
void designBandpassInto(std::vector<Complex> &out,
                        double loHz,
                        double hiHz,
                        double sampleRate,
                        int numTaps,
                        double beta = 8.0);

} // namespace dsdr::dsp
