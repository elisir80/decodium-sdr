// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — motore DeepFilterNet3 sulla C-API ufficiale (IMPL-001 §1, E2).
//
// La libreria si carica a **runtime**, non si linka, e non sta nel
// repository. È una deviazione dalla specifica — §2 chiedeva binari prebuilt
// versionati — e ha due motivi, entrambi verificati e non supposti:
//
//   1. Il progetto DeepFilterNet **non pubblica** una libreria C-API fra i
//      suoi rilasci. Pubblica un eseguibile e un plugin LADSPA; la C-API la
//      costruisce in CI con `cargo-c`, e chi la vuole se la costruisce.
//      «Vendorizzare i prebuilt» non era possibile perché i prebuilt non
//      esistono.
//
//   2. Qualunque binario di DeepFilterNet pesa fra i venticinque e i
//      cinquanta megabyte. Cinque piattaforme sono duecento megabyte dentro
//      la storia di git, per sempre — e in questo repository quella lezione è
//      già stata pagata una volta.
//
// Caricarla a runtime risolve entrambe le cose e ne risolve una terza: chi non
// ha la libreria non deve installare niente, il motore dice che non c'è e
// RNNoise resta al suo posto. È lo stesso schema del ColibriNANO, già
// collaudato qui dentro.
//
// La C-API in sé è piccola e stabile:
//
//   df_create(path, atten_lim, log_level)   crea lo stato dal modello .tar.gz
//   df_get_frame_length(state)              quanti campioni per fotogramma
//   df_process_frame(state, in, out)        elabora, e **restituisce il SNR**
//   df_set_atten_lim(state, db)             l'attenuazione, a caldo
//   df_free(state)
//
// Che `df_set_atten_lim` esista a runtime era la questione aperta №1 della
// specifica: esiste, quindi il cursore d'intensità non richiede di ricreare il
// motore.
#pragma once

#include "dsp/neural/INrEngine.h"

#include <atomic>
#include <memory>
#include <vector>

class QLibrary;

namespace dsdr::dsp::neural {

class DfnEngine : public INrEngine
{
public:
    DfnEngine();
    ~DfnEngine() override;

    bool prepare(const QString &modelPath, QString *error) override;
    void processFrame(float *samples) override;
    void setAttenuationLimitDb(float db) override;
    NrEngineInfo info() const override;
    void reset() override;

    /// Rapporto segnale/rumore dell'ultimo fotogramma, come lo stima la rete.
    /// Non è una curiosità: è la misura che dice se lo stadio sta lavorando su
    /// un segnale o su rumore, e la si può mostrare invece di far indovinare.
    float lastSnrDb() const { return m_lastSnrDb.load(std::memory_order_relaxed); }

    /// Vero se la libreria è stata trovata e i simboli risolti.
    bool isLoaded() const { return m_state != nullptr; }

    /// Dove cercare la libreria, in ordine. Esposta perché il messaggio
    /// d'errore possa dire *dove* si è guardato: «non trovata» senza l'elenco
    /// dei posti è un vicolo cieco.
    static QStringList searchPaths();

private:
    struct Api;

    std::unique_ptr<QLibrary> m_library;
    std::unique_ptr<Api> m_api;
    void *m_state = nullptr;

    std::vector<float> m_output;
    QString m_modelPath;
    QString m_modelName;
    int m_frameSamples = 0;
    float m_attenuationDb = 100.0f;
    std::atomic<float> m_lastSnrDb{0.0f};
};

} // namespace dsdr::dsp::neural
