// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — il lucchetto della pianificazione FFTW.
//
// FFTW dichiara rientrante **solo** `fftwf_execute`. Creare o distruggere un
// piano, e allocare con `fftwf_malloc`, toccano uno stato globale della
// libreria: farlo da due thread nello stesso istante corrompe l'heap, e il
// programma muore più tardi, in un punto che con la trasformata non c'entra
// niente.
//
// Il lucchetto è uno per tutto il processo perché lo stato di FFTW è uno solo:
// un mutex per classe proteggerebbe ciascuna da sé stessa e da nessun'altra.
// Gli stadi che pianificano sono lo spettro della ricezione, quello del
// monitor di trasmissione — che vive su un altro thread — e l'EMNR di ogni
// canale.
//
// Non serve nel percorso caldo: lì si esegue soltanto, e `fftwf_execute` si
// può chiamare da quanti thread si vuole.
#pragma once

#include <mutex>

namespace dsdr::dsp {

std::mutex &fftwPlanningMutex();

} // namespace dsdr::dsp
