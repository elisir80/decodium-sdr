// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — com'è messa la banda oggi, rispetto a com'è di solito.
//
// **Il problema.** «Stasera i quaranta sono rumorosi» è una frase che tutti
// dicono e che nessuno può verificare: il fondo di rumore lo si guarda adesso,
// e adesso non ha niente con cui confrontarsi. Sembra alto perché lo è, o
// perché ieri sera si era davanti a una radio diversa, o perché è sempre stato
// così e non ci si era fatto caso.
//
// Qui il fondo si annota. Non i campioni — quelli sono rumore — ma la
// **mediana su un quarto d'ora**, che è la scala su cui una condizione di
// propagazione cambia davvero.
//
// **La parte che rende il confronto onesto, e che è tutto il lavoro.**
//
// Un fondo di rumore in dBFS non è comparabile con se stesso se in mezzo è
// cambiato qualcosa nella catena. Tre cose lo cambiano, e vanno tutte e tre
// trattate:
//
//  · **Il guadagno.** Quello che si legge al convertitore è il rumore
//    dell'antenna più il guadagno. Si annota anche il guadagno, e si confronta
//    la differenza: è il rumore **riferito all'antenna**, che è la sola
//    grandezza che voglia dire qualcosa. Senza, il grafico di «com'è messa la
//    banda» sarebbe il grafico del proprio AGC.
//
//  · **Il ricevitore.** Due radio diverse hanno due fondi diversi, e non c'è
//    correzione che li renda uno. Ogni giornata porta con sé quale ricevitore
//    l'ha scritta, e giornate di ricevitori diversi non si confrontano — si
//    mostrano separate, o non si mostrano.
//
//  · **La banda.** Ovvio, e va detto lo stesso: il registro è per banda,
//    perché il fondo dei 160 non ha niente da dire su quello dei 15.
//
// **Perché non SQLite**, che la specifica indicava. Il dato ha una forma
// fissa: novantasei quarti d'ora per giornata per banda, trenta giornate.
// Sono duecentoquaranta kilobyte, e un motore di interrogazione per leggere un
// vettore di novantasei numeri è un modulo Qt in più da distribuire, un plugin
// di driver che deve arrivare nel pacchetto, e un modo nuovo di fallire il
// primo avvio su una macchina che non ce l'ha.
#pragma once

#include <QDate>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <array>
#include <vector>

namespace dsdr::core {

class BandConditions : public QObject
{
    Q_OBJECT

    /// La banda a cui si riferisce quello che si sta guardando.
    Q_PROPERTY(QString bandName READ bandName NOTIFY changed)

    /// Se la banda corrente è una di quelle che il registro conosce.
    Q_PROPERTY(bool onBand READ onBand NOTIFY changed)

    /// La curva di oggi e quella tipica, in dB riferiti all'antenna.
    /// Novantasei valori ciascuna, uno per quarto d'ora di UTC. I quarti
    /// d'ora senza dati valgono NaN, e chi disegna deve saltarli invece di
    /// tirarci una riga sopra: una riga fra due misure lontane un'ora è
    /// un'interpolazione che nessuno ha misurato.
    Q_PROPERTY(QVariantList today READ today NOTIFY changed)
    Q_PROPERTY(QVariantList typical READ typical NOTIFY changed)
    Q_PROPERTY(QVariantList yesterday READ yesterday NOTIFY changed)

    /// In quale quarto d'ora si è adesso, da 0 a 95.
    Q_PROPERTY(int currentBucket READ currentBucket NOTIFY changed)

    /// Quante giornate stanno dietro alla curva tipica. Zero significa che
    /// non c'è ancora niente da confrontare, e va detto invece di mostrare una
    /// riga piatta che sembra una misura.
    Q_PROPERTY(int typicalDays READ typicalDays NOTIFY changed)

    /// La mediana dei campioni del quarto d'ora **in corso**.
    ///
    /// Non entra nella curva — quella porta solo quarti d'ora chiusi — ma va
    /// mostrata a parte: senza, chi apre il pannello guarda un riquadro vuoto
    /// per un quarto d'ora e conclude che non funziona. NaN finché non c'è
    /// almeno una misura buona.
    Q_PROPERTY(double nowDb READ nowDb NOTIFY changed)

    /// Quanto il fondo di adesso si scosta da quello tipico di quest'ora, in
    /// dB. È il numero che risponde alla domanda: positivo vuol dire più
    /// rumorosa del solito.
    Q_PROPERTY(double departureDb READ departureDb NOTIFY changed)
    Q_PROPERTY(bool hasDeparture READ hasDeparture NOTIFY changed)

public:
    explicit BandConditions(QObject *parent = nullptr);
    ~BandConditions() override;

    /// Quanti quarti d'ora in una giornata.
    static constexpr int kBuckets = 96;

    /// Per quante giornate si tiene la storia. Trenta bastano a vedere una
    /// stagione cambiare senza tenere in memoria un anno che nessuno guarda.
    static constexpr int kHistoryDays = 30;

    QString bandName() const { return m_bandName; }
    bool onBand() const { return m_bandIndex >= 0; }
    QVariantList today() const;
    QVariantList yesterday() const;
    QVariantList typical() const;
    int currentBucket() const;
    int typicalDays() const { return m_typicalDays; }
    double nowDb() const;
    double departureDb() const { return m_departureDb; }
    bool hasDeparture() const { return m_hasDeparture; }

    /// Una misura. `floorDbfs` è il fondo al convertitore, `gainReductionDb`
    /// quanto la guardia contro la saturazione ha tolto, `deviceKey` chi sta
    /// ricevendo.
    ///
    /// Si chiama spesso e costa niente: i campioni si accumulano, e la mediana
    /// si calcola quando il quarto d'ora si chiude.
    void observe(qint64 centerHz, double floorDbfs, double gainReductionDb,
                 const QString &deviceKey);

    /// Chiude il quarto d'ora in corso senza aspettare che scada. Da chiamare
    /// prima di uscire: mezz'ora di misure buttata via perché il programma si
    /// è chiuso alle 20:07 è mezz'ora che non torna.
    void flush();

    void load();
    void save() const;

    /// Dove sta il file. Utile ai test e a chi vuole guardarci dentro.
    static QString storagePath();

signals:
    void changed();

private:
    /// Una giornata di una banda.
    struct Day
    {
        /// Le mediane, in dB riferiti all'antenna. NaN dove non si è misurato.
        std::array<float, kBuckets> floor{};
        /// Chi riceveva. Giornate di ricevitori diversi non si confrontano.
        quint32 device = 0;
    };

    static int bandFor(qint64 hz);
    static QString bandNameFor(int index);
    static quint64 keyFor(qint64 julianDay, int band);

    void closeBucket();
    void recompute();
    void prune();

    QHash<quint64, Day> m_days;

    int m_bandIndex = -1;
    QString m_bandName;
    quint32 m_device = 0;

    /// I campioni del quarto d'ora in corso, già riferiti all'antenna.
    std::vector<float> m_pending;
    qint64 m_pendingDay = 0;
    int m_pendingBucket = -1;

    int m_typicalDays = 0;
    double m_departureDb = 0.0;
    bool m_hasDeparture = false;

    /// Quante misure sono entrate da quando si è salvato l'ultima volta. Il
    /// file si riscrive ogni tanto e non a ogni campione: duecento kilobyte
    /// riscritti cinque volte al secondo sono un disco che gira per niente.
    int m_sinceSave = 0;
};

} // namespace dsdr::core
