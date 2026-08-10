// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — l'interfaccia dei motori di riduzione neurale (IMPL-001 §3).
//
// Due motori dietro un'unica interfaccia: RNNoise, che sta nel pacchetto e
// funziona offline, e DeepFilterNet3, che arriva dietro la sua C-API
// ufficiale. Quale dei due stia lavorando non deve cambiare una riga di ciò
// che gli sta attorno — lo stadio, i ring, la macchina a stati, la UI.
//
// Il contratto duro è `processFrame`: dopo `prepare` non alloca, non prende
// lock, non tocca il disco. Non è un consiglio, è la condizione perché quel
// codice possa girare su un thread con una scadenza — e c'è un test che
// conta le allocazioni per verificarlo.
//
// Entrambi i motori lavorano su fotogrammi da 480 campioni a 48 kHz, cioè
// dieci millisecondi. Non è una coincidenza fortunata: è la granularità con
// cui sono stati addestrati, e cambiarla vorrebbe dire dare alla rete un
// ingresso diverso da quello che ha imparato a leggere.
#pragma once

#include <QString>

namespace dsdr::dsp::neural {

/// Che cosa un motore dice di sé.
struct NrEngineInfo
{
    QString id;             ///< "rnnoise", "dfn3"
    QString modelName;      ///< il nome leggibile del modello caricato
    int frameSamples = 0;   ///< 480 per entrambi
    /// Ritardo algoritmico **dichiarato dal motore**, in campioni. Dichiarato
    /// e non scoperto: chi ascolta deve poter sapere di quanto è in ritardo
    /// prima di accorgersene da solo.
    int latencySamples = 0;
};

class INrEngine
{
public:
    virtual ~INrEngine() = default;

    /// Tutto ciò che è pesante — caricare il modello, allocare — sta qui e
    /// solo qui. Restituisce false e spiega perché: un motore che non parte
    /// deve dirlo, non degradare in silenzio.
    virtual bool prepare(const QString &modelPath, QString *error) = 0;

    /// Un fotogramma esatto di `frameSamples` campioni mono a 48 kHz,
    /// elaborato sul posto. Real-time safe.
    virtual void processFrame(float *samples) = 0;

    /// Quanto attenuare, in dB: zero è passaggio morbido, cento è pieno.
    virtual void setAttenuationLimitDb(float db) = 0;

    virtual NrEngineInfo info() const = 0;

    /// Azzera gli stati ricorrenti. Va chiamata a ogni cambio di canale o di
    /// modo: la memoria di un contesto non deve colorare il successivo, e una
    /// rete che si porta dietro il rumore di una banda diversa produce
    /// artefatti che sembrano difetti del segnale.
    virtual void reset() = 0;
};

} // namespace dsdr::dsp::neural
