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
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY colorsChanged)
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
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &color);

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
    QColor m_backgroundColor{0x07, 0x0B, 0x11};
    qreal m_viewStart = 0.0;
    qreal m_viewSpan = 1.0;

    WaterfallMode m_waterfallMode = Flat;
    int m_paletteIndex = 0;
    qreal m_gamma = 0.85;
    qreal m_blackThreshold = 0.06;
    qreal m_tilt = 58.0;
    qreal m_rotation3d = 0.0;
    qreal m_reliefScale = 0.45;

    bool m_autoRange = false;
    qreal m_noiseFloorDb = -130.0;
    qreal m_peakLevelDb = -20.0;
    bool m_levelsSeeded = false;
    bool m_publishPending = false;
    std::vector<float> m_levelScratch;  ///< copia ordinabile della riga

    // ── Misura del ritmo delle righe ─────────────────────────────────────
    QElapsedTimer m_rowClock;
    int m_rowsSinceTick = 0;
    int m_historyRows = 0;
    qreal m_rowsPerSecond = 0.0;
    bool m_ratePublishPending = false;
};

} // namespace dsdr::app
