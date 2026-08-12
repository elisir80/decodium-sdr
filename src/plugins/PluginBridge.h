// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — il lato nostro dell'ospite dei plugin (SPEC-005 §4.5).
//
// Parla con `decodium-vst-host` e si comporta come uno stadio della catena:
// gli si dà un blocco di campioni, torna elaborato. La differenza rispetto a
// tutti gli altri stadi è che questo può **morire**, e la cosa importante di
// questa classe è come si comporta quando succede.
//
// **Se l'ospite muore, il blocco va in bypass e basta.** Non un errore fatale,
// non una trasmissione interrotta: il segnale passa dritto, si accende un
// avviso, e la radio continua a fare la radio. Un plugin che sbaglia un indice
// non deve poter zittire una stazione che sta chiamando.
//
// **Non si riavvia da solo.** Un ospite che risorge a ogni crash, se il plugin
// va in crash a ogni blocco, diventa un ciclo che consuma la macchina mentre
// l'operatore non capisce perché il computer è fermo. Si riavvia quando
// qualcuno lo chiede.
//
// **Thread.** `process()` lo chiama il thread del motore TX. Tutto il resto —
// caricare, elencare, cambiare parametri — arriva dal thread della UI, e passa
// da una coda: il thread TX non aspetta mai un processo esterno per un
// comando, aspetta solo per i campioni.
#pragma once

#include <QObject>
#include <QProcess>
#include <QSharedMemory>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <vector>

namespace dsdr::plugins {

/// Un plugin trovato sul disco.
struct PluginInfo
{
    QString path;
    QString name;
    QString vendor;
    QString category;
};

/// Un parametro esposto dal plugin caricato.
struct PluginParameter
{
    int index = 0;
    QString name;
    QString unit;
    double value = 0.0;   ///< normalizzato 0…1, come vuole VST3
};

class PluginBridge : public QObject
{
    Q_OBJECT

public:
    explicit PluginBridge(QObject *parent = nullptr);
    ~PluginBridge() override;

    /// Se l'eseguibile ospite esiste. Senza, non c'è niente da mostrare: il
    /// blocco non compare (CONSTITUTION §7).
    static bool hostAvailable();

    /// Avvia l'ospite. Torna `false` e dice perché se non parte.
    bool start();
    void stop();
    bool isRunning() const;

    /// Elenca i plugin nelle cartelle di sistema.
    QList<PluginInfo> scan();

    /// Carica un plugin. Un percorso vuoto lo scarica.
    bool load(const QString &path);
    QString loadedPath() const { return m_loadedPath; }
    QString loadedName() const { return m_loadedName; }

    QList<PluginParameter> parameters() const { return m_parameters; }
    void setParameter(int index, double value);

    void prepare(double sampleRate, int maxFrames);

    /// Acceso o spento. Spento non attraversa il processo: il costo del giro
    /// c'è comunque, e uno stadio in bypass deve costare zero.
    void setEnabled(bool enabled)
    {
        m_enabled.store(enabled, std::memory_order_relaxed);
    }
    bool isEnabled() const { return m_enabled.load(std::memory_order_relaxed); }

    /// Elabora sul posto. Chiamato dal thread del motore TX.
    ///
    /// Se l'ospite non c'è, è morto, o tarda oltre il tempo concesso, il
    /// segnale esce com'è entrato. Non torna un errore: torna il silenzio del
    /// bypass, che sul percorso caldo è l'unica risposta utile.
    void process(float *audio, std::size_t frames) noexcept;

    /// L'ultimo guaio, per l'operatore. Vuoto se va tutto bene.
    QString lastError() const { return m_lastError; }

    /// Quante volte l'ospite è morto sotto di noi. Un numero che cresce dice
    /// «il plugin non regge», e lo dice meglio di qualunque messaggio.
    quint64 crashCount() const { return m_crashes; }

signals:
    void stateChanged();

    /// L'ospite è morto. La UI lo mostra invece di lasciare un blocco acceso
    /// che non fa più niente.
    void hostDied(const QString &reason);

private:
    /// Manda un comando e aspetta la riga di risposta.
    QStringList command(const QString &line, int timeoutMs = 3000);

    void handleDeath();

    std::unique_ptr<QProcess> m_process;
    std::unique_ptr<QSharedMemory> m_shared;
    QString m_key;

    QString m_loadedPath;
    QString m_loadedName;
    QList<PluginParameter> m_parameters;
    QString m_lastError;
    quint64 m_crashes = 0;

    double m_sampleRate = 48000.0;
    int m_maxFrames = 1024;

    std::atomic<bool> m_enabled{false};

    /// Vero solo quando c'è un plugin caricato e l'ospite risponde. Lo legge
    /// il thread TX a ogni blocco, quindi è atomico.
    std::atomic<bool> m_live{false};

    std::vector<float> m_scratch;
};

} // namespace dsdr::plugins
