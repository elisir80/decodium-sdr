// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — chi c'è sulla rete.
//
// Non è un backend e non ne diventerà uno: non apre niente, non consegna
// campioni, non ha capability. Fa una cosa sola — dire quali radio sono
// raggiungibili — e la fa per famiglie che DECODIUM SDR non sa ancora aprire.
//
// Perché esiste. Chi collega un Flex o un Hermes-Lite alla rete e non lo vede
// da nessuna parte non ha modo di sapere se il problema è la radio, il cavo,
// il firewall o il programma. Con questo, la domanda ha una risposta: «c'è, è
// a questo indirizzo, questa versione di firmware — e questo programma non la
// apre ancora». È un'informazione, non una promessa: nell'elenco dei device
// non compare nulla, perché quello elenca ciò che si può usare
// (CONSTITUTION §7).
//
// Sta nella HAL e non nel core perché è conoscenza di protocolli, ed è lì che
// la conoscenza dei protocolli vive.
#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>

#include <memory>

class QTimer;
class QUdpSocket;

namespace dsdr::hal {

/// Una radio vista in rete.
struct ScoutedRadio
{
    QString family;    ///< "OpenHPSDR", "FlexRadio", …
    QString model;     ///< "Hermes-Lite 2", "FLEX-6400", …
    QString address;
    QString detail;    ///< firmware, numero di serie, quello che dice di sé
    QString identity;  ///< chiave stabile: MAC o numero di serie

    bool isValid() const noexcept { return !family.isEmpty() && !identity.isEmpty(); }
};

class RadioScout : public QObject
{
    Q_OBJECT

public:
    explicit RadioScout(QObject *parent = nullptr);
    ~RadioScout() override;

    /// Comincia ad ascoltare e manda le chiamate. Dura `seconds` e poi tace:
    /// un ascolto perpetuo su una porta condivisa toglierebbe i pacchetti al
    /// programma del costruttore, che magari sta girando accanto al nostro.
    void start(int seconds = 6);
    void stop();
    bool isScanning() const;

signals:
    void radioFound(const dsdr::hal::ScoutedRadio &radio);
    void finished();

public:
    // ── Interpretazione delle risposte ──────────────────────────────────
    //
    // Statiche e pure: sono la parte che si sbaglia in silenzio — un byte
    // preso dall'offset sbagliato produce un modello inventato invece di un
    // errore — e vanno verificate senza una radio in rete.

    /// Risposta al discovery OpenHPSDR (Metis, Hermes, Angelia, Orion,
    /// Hermes-Lite). Vuota se il pacchetto non è una risposta valida.
    static ScoutedRadio parseHpsdrReply(const QByteArray &datagram,
                                        const QHostAddress &sender);

    /// Annuncio di un FlexRadio serie 6000. La radio lo trasmette da sé, una
    /// volta al secondo: qui non si chiede niente, si ascolta e basta.
    static ScoutedRadio parseFlexAnnounce(const QByteArray &datagram,
                                          const QHostAddress &sender);

    /// Il pacchetto di chiamata OpenHPSDR, per chi lo vuole verificare.
    static QByteArray hpsdrProbe();

private slots:
    void readHpsdr();
    void readFlex();

private:
    void broadcastHpsdr();
    void report(const ScoutedRadio &radio);

    std::unique_ptr<QUdpSocket> m_hpsdr;
    std::unique_ptr<QUdpSocket> m_flex;
    QTimer *m_probeTimer = nullptr;
    QTimer *m_stopTimer = nullptr;
    QStringList m_seen;
};

} // namespace dsdr::hal

Q_DECLARE_METATYPE(dsdr::hal::ScoutedRadio)
