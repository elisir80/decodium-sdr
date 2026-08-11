// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — spettro + waterfall su GPU (§6.1).
//
// Un solo QQuickRhiItem rende entrambi. La spec §6.2 immagina due componenti
// distinti, ma il feed è un ring SPSC a consumatore unico: due item che lo
// leggessero in parallelo si ruberebbero le righe a vicenda. Le sovrapposizioni
// (griglia, flag VFO, etichette) restano QML puro, sopra questo item.
//
// Waterfall: texture ad anello, una riga nuova per frame, scorrimento via
// offset UV — la storia non viene mai ridisegnata.
#pragma once

#include "core/SpectrumFeed.h"

#include <QColor>
#include <QElapsedTimer>
#include <QQuickRhiItem>

#include <vector>

namespace dsdr::app {

class PanadapterView : public QQuickRhiItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PanadapterView)

    Q_PROPERTY(dsdr::core::SpectrumFeed *feed READ feed WRITE setFeed NOTIFY feedChanged)
    Q_PROPERTY(qreal floorDb READ floorDb WRITE setFloorDb NOTIFY levelRangeChanged)
    Q_PROPERTY(qreal ceilingDb READ ceilingDb WRITE setCeilingDb NOTIFY levelRangeChanged)
    Q_PROPERTY(qreal spectrumRatio READ spectrumRatio WRITE setSpectrumRatio NOTIFY spectrumRatioChanged)
    Q_PROPERTY(QColor traceColor READ traceColor WRITE setTraceColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor peakColor READ peakColor WRITE setPeakColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY colorsChanged)
    Q_PROPERTY(bool peakHold READ peakHold WRITE setPeakHold NOTIFY peakHoldChanged)
    Q_PROPERTY(qreal peakDecayDb READ peakDecayDb WRITE setPeakDecayDb NOTIFY peakHoldChanged)
    Q_PROPERTY(qreal viewStart READ viewStart WRITE setViewStart NOTIFY viewChanged)
    Q_PROPERTY(qreal viewSpan READ viewSpan WRITE setViewSpan NOTIFY viewChanged)
    Q_PROPERTY(WaterfallMode waterfallMode READ waterfallMode WRITE setWaterfallMode NOTIFY waterfallModeChanged)
    Q_PROPERTY(int paletteIndex READ paletteIndex WRITE setPaletteIndex NOTIFY paletteChanged)
    Q_PROPERTY(QStringList paletteNames READ paletteNames CONSTANT)
    Q_PROPERTY(qreal gamma READ gamma WRITE setGamma NOTIFY toneChanged)
    Q_PROPERTY(qreal blackThreshold READ blackThreshold WRITE setBlackThreshold NOTIFY toneChanged)
    Q_PROPERTY(qreal tilt READ tilt WRITE setTilt NOTIFY sceneChanged)
    Q_PROPERTY(qreal rotation3d READ rotation3d WRITE setRotation3d NOTIFY sceneChanged)
    Q_PROPERTY(qreal reliefScale READ reliefScale WRITE setReliefScale NOTIFY sceneChanged)
    Q_PROPERTY(qreal reliefGrid READ reliefGrid WRITE setReliefGrid NOTIFY sceneChanged)
    Q_PROPERTY(qreal floorFlattening READ floorFlattening WRITE setFloorFlattening NOTIFY toneChanged)
    Q_PROPERTY(qreal timeSpan READ timeSpan WRITE setTimeSpan NOTIFY timeViewChanged)
    Q_PROPERTY(bool frozen READ frozen WRITE setFrozen NOTIFY timeViewChanged)
    Q_PROPERTY(bool autoRange READ autoRange WRITE setAutoRange NOTIFY autoRangeChanged)
    Q_PROPERTY(qreal noiseFloorDb READ noiseFloorDb NOTIFY measuredLevelsChanged)
    Q_PROPERTY(qreal peakLevelDb READ peakLevelDb NOTIFY measuredLevelsChanged)
    Q_PROPERTY(qreal historySeconds READ historySeconds NOTIFY historySecondsChanged)

public:
    /// Come si disegna la storia dello spettro.
    ///
    /// Non è una preferenza estetica: la vista piatta legge meglio le
    /// frequenze e la densità del traffico, quella in rilievo legge meglio
    /// l'andamento nel tempo di un singolo segnale.
    enum WaterfallMode {
        Flat = 0,       ///< Waterfall classico, visto dall'alto
        Relief = 1,     ///< Superficie in prospettiva: il tempo si allontana
    };
    Q_ENUM(WaterfallMode)

    explicit PanadapterView(QQuickItem *parent = nullptr);

    /// Quanto sopra il fondo misurato si posa il fondo della scala.
    ///
    /// Il verso di questo margine decide se il waterfall si legge. Con il fondo
    /// della scala *sotto* il rumore — come faceva questa classe con −6 dB —
    /// ogni bin di rumore riceve un colore; siccome il rumore occupa quasi
    /// tutta la banda, l'immagine risulta uniformemente accesa e i segnali non
    /// staccano più. Mettendolo *sopra*, il rumore ricade nella zona nera e il
    /// colore torna a significare «qui c'è qualcosa».
    ///
    /// Il valore va letto insieme al percentile su cui si misura il fondo (il
    /// 30°, circa una deviazione standard sotto la mediana del rumore): tre
    /// decibel sopra quel percentile cadono attorno alla mediana, ed è lì che
    /// vogliamo il nero.
    ///
    /// Il prezzo è dichiarato: un segnale che sta meno di tre decibel sopra il
    /// rumore viene tagliato via. Chi cerca proprio quello abbassa il cursore
    /// del fondo — è il mestiere di quel cursore — mentre il valore di fabbrica
    /// deve dare un'immagine leggibile a chi apre l'applicazione.
    static constexpr qreal kFloorAboveNoiseDb = 3.0;

    /// Quanti decibel di respiro del rumore devono restare neri.
    ///
    /// Il rumore di una FFT non mediata oscilla di parecchi decibel da una riga
    /// all'altra. Fino a questa quota sopra il fondo misurato non deve accendere
    /// nulla, altrimenti il waterfall pulsa. È il contratto che il test
    /// verifica: alla scala scelta, un livello di `kNoiseHeadroomDb` sopra il
    /// fondo misurato deve normalizzarsi sotto la soglia di nero.
    static constexpr qreal kNoiseHeadroomDb = 6.0;

    QQuickRhiItemRenderer *createRenderer() override;

    core::SpectrumFeed *feed() const { return m_feed; }
    void setFeed(core::SpectrumFeed *feed);

    qreal floorDb() const { return m_floorDb; }
    void setFloorDb(qreal db);
    qreal ceilingDb() const { return m_ceilingDb; }
    void setCeilingDb(qreal db);

    qreal spectrumRatio() const { return m_spectrumRatio; }
    void setSpectrumRatio(qreal ratio);

    QColor traceColor() const { return m_traceColor; }
    void setTraceColor(const QColor &color);
    QColor fillColor() const { return m_fillColor; }
    void setFillColor(const QColor &color);
    QColor peakColor() const { return m_peakColor; }
    void setPeakColor(const QColor &color);
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &color);

    /// Tenuta dei picchi: una seconda traccia che segna il massimo raggiunto da
    /// ogni bin e scende piano.
    ///
    /// La traccia istantanea dice cosa c'è *adesso*; su una banda dove i
    /// segnali vanno e vengono — un CQ, una risposta, una portante che si apre
    /// — quel «adesso» è quasi sempre il momento sbagliato. La riga dei massimi
    /// tiene insieme gli ultimi secondi e mostra dov'era occupato lo spettro,
    /// che è l'informazione con cui si decide dove sintonizzare.
    bool peakHold() const { return m_peakHold; }
    void setPeakHold(bool enabled);

    /// Con quanti decibel al secondo scende la riga dei massimi.
    ///
    /// In decibel al secondo e non per riga: il ritmo delle righe dipende dalla
    /// banda, dalla FFT e da quante trasformate si mediano, e una discesa
    /// legata a quello cambierebbe velocità ogni volta che si tocca la media.
    /// Quello che l'occhio giudica è quanto tempo un picco resta lì.
    qreal peakDecayDb() const { return m_peakDecayDb; }
    void setPeakDecayDb(qreal dbPerSecond);

    /// Porzione di banda mostrata, come frazioni di 0..1. `viewStart` è il
    /// bordo sinistro, `viewSpan` la larghezza: insieme sono lo zoom.
    qreal viewStart() const { return m_viewStart; }
    void setViewStart(qreal value);
    qreal viewSpan() const { return m_viewSpan; }
    void setViewSpan(qreal value);

    WaterfallMode waterfallMode() const { return m_waterfallMode; }
    void setWaterfallMode(WaterfallMode mode);

    int paletteIndex() const { return m_paletteIndex; }
    void setPaletteIndex(int index);
    QStringList paletteNames() const;

    /// Sotto 1 fa emergere i segnali deboli senza spostare la soglia.
    qreal gamma() const { return m_gamma; }
    void setGamma(qreal value);

    /// Livello sotto il quale tutto resta fondo, in frazione della scala.
    qreal blackThreshold() const { return m_blackThreshold; }
    void setBlackThreshold(qreal value);

    // ── Solo per la vista in rilievo ─────────────────────────────────────
    qreal tilt() const { return m_tilt; }
    void setTilt(qreal degrees);
    qreal rotation3d() const { return m_rotation3d; }
    void setRotation3d(qreal degrees);
    qreal reliefScale() const { return m_reliefScale; }
    void setReliefScale(qreal value);

    /// Quanto marcata è la griglia disegnata sulla superficie in rilievo.
    ///
    /// In prospettiva la stessa distanza sullo schermo vale frequenze diverse a
    /// seconda della profondità, e senza un reticolo su cui appoggiarsi non c'è
    /// modo di dire dove cade una cresta: si vede *che* c'è un segnale, non
    /// *dove*. È il difetto che rende il 3D un ornamento nella maggior parte
    /// dei programmi.
    qreal reliefGrid() const { return m_reliefGrid; }
    void setReliefGrid(qreal value);

    /// Quanto si toglie al waterfall la pendenza del proprio fondo, da 0 a 1.
    ///
    /// Il rumore non è piatto lungo la banda: un disturbo locale, la risposta
    /// del preselettore, una emittente vicina alzano il fondo su una porzione
    /// di spettro e non sulle altre. La scala però è una sola, e tarata sul
    /// fondo *medio*: dove il fondo è più alto il colore si accende ovunque e i
    /// segnali deboli ci scompaiono dentro; dove è più basso resta tutto nero e
    /// non si vede più nemmeno quello che c'è.
    ///
    /// Con l'appiattimento si stima il fondo bin per bin e se ne toglie lo
    /// scarto dalla media. Il waterfall torna leggibile su tutta la larghezza.
    ///
    /// Vale **solo per il waterfall**, mai per la traccia: la traccia è la
    /// misura, e si legge contro una scala in decibel. Correggerla vorrebbe
    /// dire mostrare un livello che il ricevitore non ha misurato, ed è il modo
    /// più diretto di passare a qualcuno un rapporto sbagliato.
    qreal floorFlattening() const { return m_floorFlattening; }
    void setFloorFlattening(qreal value);

    /// Quanta storia entra nell'altezza del waterfall, da 0 a 1.
    ///
    /// A 1 si vede tutta la memoria disponibile; sotto, le righe si allargano e
    /// se ne vedono meno. Non cambia il ritmo con cui arrivano — quello lo
    /// decide il DSP — cambia quanto spazio si dà a ciascuna: è lo zoom
    /// dell'asse dei tempi, e serve a leggere la struttura di una trasmissione
    /// breve, dove a piena storia una sillaba è alta due pixel.
    qreal timeSpan() const { return m_timeSpan; }
    void setTimeSpan(qreal value);

    /// Ferma l'immagine senza fermare la radio.
    ///
    /// Le righe continuano ad arrivare e vengono consumate — se non lo fossero
    /// il ring si riempirebbe e il DSP comincerebbe a scartare — ma non si
    /// scrivono più: quello che c'è sullo schermo resta lì, e si può guardarlo,
    /// misurarlo, fotografarlo. Serve tutte le volte che qualcosa passa e non
    /// si fa in tempo a leggerlo.
    bool frozen() const { return m_frozen; }
    void setFrozen(bool frozen);

    /// Il livello in dB alla posizione indicata nella banda, da 0 a 1.
    ///
    /// È l'ultima riga di spettro misurata, la stessa da cui nascono la traccia
    /// e le misure di fondo e picco: serve al cursore che legge lo spettro
    /// sotto il puntatore. Restituisce il valore del fondo scala se non c'è
    /// ancora nulla da leggere.
    Q_INVOKABLE qreal levelAt(qreal bandFraction) const;

    /// Con l'auto-range attivo, `floorDb` e `ceilingDb` smettono di essere
    /// comandi e diventano il risultato della misura: la UI continua a
    /// leggerli allo stesso modo, ma non li scrive più.
    bool autoRange() const { return m_autoRange; }
    void setAutoRange(bool enabled);

    /// Livello del rumore di fondo e del segnale più forte, in dB, misurati
    /// sull'ultima riga di spettro. Sempre aggiornati, anche a auto-range
    /// spento: servono a mostrare all'utente dove starebbe la scala.
    qreal noiseFloorDb() const { return m_noiseFloorDb; }
    qreal peakLevelDb() const { return m_peakLevelDb; }

    /// Quanti secondi di storia mostra il waterfall, o 0 se non lo sappiamo
    /// ancora.
    ///
    /// Il waterfall tiene un numero fisso di righe; quanto tempo coprano
    /// dipende da quante ne arrivano al secondo, che a sua volta dipende dalla
    /// banda, dalla dimensione della FFT e dal ritmo del rendering. È una
    /// misura, non una costante: un asse dei tempi ricavato da un valore
    /// supposto direbbe numeri sbagliati con la stessa sicurezza di quelli
    /// giusti.
    qreal historySeconds() const;

    /// Chiamata dal thread di rendering dentro `synchronize()`, l'unico punto
    /// in cui il thread GUI è fermo. Non emette nulla direttamente: accoda.
    void reportMeasuredLevels(const std::vector<float> &row);

    /// Quante righe di waterfall sono state consumate in questo frame.
    /// Stesso contratto di `reportMeasuredLevels`: render thread, niente
    /// signal emessi da qui.
    void reportRowsConsumed(int rows, int historyRows);

signals:
    void feedChanged();
    void viewChanged();
    void waterfallModeChanged();
    void paletteChanged();
    void toneChanged();
    void sceneChanged();
    void levelRangeChanged();
    void spectrumRatioChanged();
    void colorsChanged();
    void autoRangeChanged();
    void measuredLevelsChanged();
    void historySecondsChanged();
    void peakHoldChanged();
    void timeViewChanged();

private:
    /// Pubblica sul thread GUI le misure raccolte dal render thread.
    Q_INVOKABLE void publishMeasuredLevels();
    Q_INVOKABLE void publishHistorySeconds();

    core::SpectrumFeed *m_feed = nullptr;
    QMetaObject::Connection m_feedConnection;

    qreal m_floorDb = -130.0;
    qreal m_ceilingDb = -20.0;
    qreal m_spectrumRatio = 0.42;
    QColor m_traceColor{0x6E, 0xE7, 0xFF};
    QColor m_fillColor{0x1E, 0x88, 0xC7};
    QColor m_peakColor{0xFF, 0xC8, 0x6E};
    QColor m_backgroundColor{0x07, 0x0B, 0x11};
    bool m_peakHold = true;
    qreal m_peakDecayDb = 12.0;
    qreal m_viewStart = 0.0;
    qreal m_viewSpan = 1.0;

    WaterfallMode m_waterfallMode = Flat;
    int m_paletteIndex = 0;
    qreal m_gamma = 0.85;
    qreal m_blackThreshold = 0.06;
    qreal m_tilt = 58.0;
    qreal m_rotation3d = 0.0;
    qreal m_reliefScale = 0.45;
    qreal m_reliefGrid = 0.35;
    qreal m_floorFlattening = 0.0;
    qreal m_timeSpan = 1.0;
    bool m_frozen = false;

    bool m_autoRange = false;
    qreal m_noiseFloorDb = -130.0;
    qreal m_peakLevelDb = -20.0;
    bool m_levelsSeeded = false;
    bool m_publishPending = false;
    std::vector<float> m_levelScratch;  ///< copia ordinabile della riga

    /// L'ultima riga misurata, così com'è arrivata.
    ///
    /// La scrive il render thread dentro `synchronize()`, quando il thread GUI
    /// è fermo; la legge `levelAt()` dal thread GUI. Non serve un lock proprio
    /// perché i due momenti non possono sovrapporsi — è lo stesso contratto
    /// che regge tutto il resto di questa classe.
    std::vector<float> m_lastRow;

    // ── Misura del ritmo delle righe ─────────────────────────────────────
    QElapsedTimer m_rowClock;
    int m_rowsSinceTick = 0;
    int m_historyRows = 0;
    qreal m_rowsPerSecond = 0.0;
    bool m_ratePublishPending = false;
};

} // namespace dsdr::app
