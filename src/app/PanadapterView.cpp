// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/PanadapterView.h"
#include "app/PanadapterRenderer.h"
#include "app/WaterfallPalette.h"

#include <algorithm>
#include <cmath>

namespace dsdr::app {

namespace {

/// Percentile che consideriamo "fondo". Non la mediana: con la banda piena di
/// portanti la mediana sale con il traffico, e la scala si chiuderebbe proprio
/// quando c'è più da vedere.
constexpr float kNoisePercentile = 0.30f;

/// Percentile del picco. Non il massimo: un singolo bin sporco — uno spurio,
/// un impulso — allargherebbe la scala schiacciando tutto il resto. In cambio,
/// un segnale strettissimo su banda vuota non apre la scala da solo e satura
/// in cima: è il verso giusto in cui sbagliare, perché resta comunque visibile.
constexpr float kPeakPercentile = 0.995f;

/// Il fondo si muove piano in entrambe le direzioni: è una proprietà del sito
/// di ricezione, non del segnale, e non deve inseguire il traffico.
constexpr qreal kNoiseSmoothing = 0.08;

/// Il picco si apre in fretta e si richiude piano: un segnale forte deve
/// entrare subito nella scala, ma una pausa nel traffico non deve far
/// respirare l'immagine.
constexpr qreal kPeakAttack = 0.50;
constexpr qreal kPeakRelease = 0.03;

/// Sotto questa variazione non vale la pena svegliare il thread GUI.
constexpr qreal kPublishThresholdDb = 0.25;

/// Il minimo intervallo utile della scala, quando fondo e picco coincidono.
constexpr qreal kMinimumSpanDb = 25.0;

/// Distanza minima fra fondo e vetta, comunque li si imposti.
///
/// Più permissiva di `kMinimumSpanDb`, che è la scala che l'auto-range *sceglie*:
/// questa è la scala che il progetto si rifiuta di scendere sotto. Chi vuole
/// spremere la dinamica su una manciata di decibel deve poterlo fare; sotto i
/// dieci, però, ogni bin diventa o nero o saturo e l'immagine non dice più
/// niente — e ci si arriva per sbaglio, portando un cursore a fondo corsa.
constexpr qreal kMinimumUsableSpanDb = 10.0;

/// Margine sopra il picco misurato: un segnale in arrivo non deve saturare
/// prima che la scala abbia il tempo di aprirsi.
constexpr qreal kCeilingAbovePeakDb = 10.0;

/// Finestra su cui si contano le righe per ricavarne il ritmo.
constexpr qint64 kRateWindowMs = 1000;

/// Il ritmo si muove piano: cambia solo quando cambia la banda o la FFT, e un
/// asse dei tempi che oscilla a ogni frame è illeggibile.
constexpr qreal kRateSmoothing = 0.25;

} // namespace

PanadapterView::PanadapterView(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
    setMirrorVertically(false);
    setSampleCount(1); // il waterfall non ha bordi da antialiasare
}

QQuickRhiItemRenderer *PanadapterView::createRenderer()
{
    return new PanadapterRenderer;
}

void PanadapterView::setFeed(core::SpectrumFeed *feed)
{
    if (m_feed == feed)
        return;

    disconnect(m_feedConnection);
    m_feed = feed;

    if (m_feed) {
        // Ridisegniamo solo quando c'è davvero una riga nuova: nessun timer
        // che consuma GPU mentre la banda è ferma.
        m_feedConnection = connect(m_feed, &core::SpectrumFeed::framesAvailable,
                                   this, [this] { update(); });
        m_feed->setLevelRange(static_cast<float>(m_floorDb), static_cast<float>(m_ceilingDb));
    }

    emit feedChanged();
    update();
}

void PanadapterView::setFloorDb(qreal db)
{
    // La vetta cede il passo, ma non si lascia scavalcare: una scala più
    // stretta di kMinimumSpanDb rende ogni bin o nero o saturo, e l'immagine
    // smette di dire alcunché. Con la scala automatica il minimo era già
    // garantito dal calcolo; in manuale non lo garantiva nessuno, e bastava
    // portare un cursore a fondo corsa per ritrovarsi tre decibel di
    // dinamica e un waterfall in fiamme.
    db = std::min(db, m_ceilingDb - kMinimumUsableSpanDb);

    if (qFuzzyCompare(m_floorDb, db))
        return;
    m_floorDb = db;
    if (m_feed)
        m_feed->setLevelRange(static_cast<float>(m_floorDb), static_cast<float>(m_ceilingDb));
    emit levelRangeChanged();
    update();
}

void PanadapterView::setCeilingDb(qreal db)
{
    // Simmetrico a setFloorDb: vedi il commento là.
    db = std::max(db, m_floorDb + kMinimumUsableSpanDb);

    if (qFuzzyCompare(m_ceilingDb, db))
        return;
    m_ceilingDb = db;
    if (m_feed)
        m_feed->setLevelRange(static_cast<float>(m_floorDb), static_cast<float>(m_ceilingDb));
    emit levelRangeChanged();
    update();
}

void PanadapterView::setSpectrumRatio(qreal ratio)
{
    ratio = qBound(0.1, ratio, 0.9);
    if (qFuzzyCompare(m_spectrumRatio, ratio))
        return;
    m_spectrumRatio = ratio;
    emit spectrumRatioChanged();
    update();
}

void PanadapterView::setViewStart(qreal value)
{
    // Il bordo sinistro non può portare la finestra fuori dalla banda.
    value = qBound(0.0, value, 1.0 - m_viewSpan);
    if (qFuzzyCompare(m_viewStart, value))
        return;
    m_viewStart = value;
    // Cambiando porzione della banda cambia anche il fondo di riferimento.
    // Tenerlo dal tratto precedente lascerebbe l'autoscala quasi immobile per
    // diversi secondi, proprio quando l'operatore ha chiesto di guardare altro.
    m_levelsSeeded = false;
    emit viewChanged();
    update();
}

void PanadapterView::setViewSpan(qreal value)
{
    // Sotto lo 0,5% di banda non si guadagna leggibilità: i bin dello spettro
    // finiscono per essere meno dei pixel.
    value = qBound(0.005, value, 1.0);
    if (qFuzzyCompare(m_viewSpan, value))
        return;
    m_viewSpan = value;
    if (m_viewStart + m_viewSpan > 1.0)
        m_viewStart = 1.0 - m_viewSpan;
    // Vedi setViewStart(): la nuova finestra deve partire dalla sua misura,
    // non dalla media lenta della finestra che l'ha preceduta.
    m_levelsSeeded = false;
    emit viewChanged();
    update();
}

void PanadapterView::setAutoRangeStart(qreal value)
{
    value = qBound(0.0, value, 1.0 - m_autoRangeSpan);
    if (qFuzzyCompare(m_autoRangeStart, value))
        return;
    m_autoRangeStart = value;
    // La banda laterale attiva è cambiata: il vecchio fondo non è una misura
    // della nuova regione, perciò non va trascinato dalla media esponenziale.
    m_levelsSeeded = false;
    emit autoRangeWindowChanged();
    update();
}

void PanadapterView::setAutoRangeSpan(qreal value)
{
    value = qBound(0.005, value, 1.0);
    if (qFuzzyCompare(m_autoRangeSpan, value))
        return;
    m_autoRangeSpan = value;
    if (m_autoRangeStart + m_autoRangeSpan > 1.0)
        m_autoRangeStart = 1.0 - m_autoRangeSpan;
    m_levelsSeeded = false;
    emit autoRangeWindowChanged();
    update();
}

void PanadapterView::setMirrorSideband(bool enabled)
{
    if (m_mirrorSideband == enabled)
        return;
    m_mirrorSideband = enabled;
    emit displayMappingChanged();
    update();
}

void PanadapterView::setMirrorLowerSideband(bool lower)
{
    if (m_mirrorLowerSideband == lower)
        return;
    m_mirrorLowerSideband = lower;
    emit displayMappingChanged();
    update();
}

qreal PanadapterView::sourceFractionForDisplay(qreal displayedFraction) const
{
    const qreal fraction = qBound<qreal>(0.0, displayedFraction, 1.0);
    if (!m_mirrorSideband)
        return fraction;

    const bool copiedHalf = m_mirrorLowerSideband ? fraction >= 0.5 : fraction < 0.5;
    return copiedHalf ? 1.0 - fraction : fraction;
}

void PanadapterView::setAutoRange(bool enabled)
{
    if (m_autoRange == enabled)
        return;
    m_autoRange = enabled;
    emit autoRangeChanged();
    if (m_autoRange) {
        // Ripartiamo dalla misura corrente invece di inseguire da dove
        // l'utente aveva lasciato i cursori a mano.
        m_levelsSeeded = false;
    }
    update();
}

void PanadapterView::reportMeasuredLevels(const std::vector<float> &row,
                                          qreal viewStart, qreal viewSpan)
{
    if (row.empty())
        return;

    // La riga si conserva così com'è: `m_levelScratch` viene riordinato dai
    // nth_element qui sotto, e non è più buono per dire che livello c'è a una
    // certa frequenza. I percentili invece usano solo la finestra visibile:
    // su audio+CAT il codec porta 48 kHz, ma la UI ne mostra 7 kHz attorno al
    // VFO. Includere i 41 kHz vuoti farebbe sprofondare il fondo e colorare
    // tutta la passata audio di giallo o rosso.
    m_lastRow = row;

    const auto rowSize = row.size();
    const qreal start = qBound<qreal>(0.0, viewStart, 1.0);
    const qreal span = qBound<qreal>(0.0, viewSpan, 1.0 - start);
    const std::size_t first = std::min<std::size_t>(
        static_cast<std::size_t>(std::floor(start * static_cast<qreal>(rowSize))),
        rowSize - 1);
    const std::size_t end = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::ceil((start + span) * static_cast<qreal>(rowSize))),
        first + 1, rowSize);
    m_levelScratch.assign(row.begin() + static_cast<std::ptrdiff_t>(first),
                          row.begin() + static_cast<std::ptrdiff_t>(end));
    const auto count = m_levelScratch.size();

    const auto noiseAt = static_cast<std::size_t>(
        std::clamp<float>(kNoisePercentile * static_cast<float>(count - 1), 0.0f,
                          static_cast<float>(count - 1)));
    const auto peakAt = static_cast<std::size_t>(
        std::clamp<float>(kPeakPercentile * static_cast<float>(count - 1), 0.0f,
                          static_cast<float>(count - 1)));

    std::nth_element(m_levelScratch.begin(), m_levelScratch.begin() + noiseAt, m_levelScratch.end());
    const auto noise = static_cast<qreal>(m_levelScratch[noiseAt]);

    // Il secondo nth_element lavora solo sulla coda alta: la prima chiamata ha
    // già separato i due gruppi.
    std::nth_element(m_levelScratch.begin() + noiseAt, m_levelScratch.begin() + peakAt,
                     m_levelScratch.end());
    const auto peak = static_cast<qreal>(m_levelScratch[peakAt]);

    if (!std::isfinite(noise) || !std::isfinite(peak))
        return;

    if (!m_levelsSeeded) {
        m_noiseFloorDb = noise;
        m_peakLevelDb = peak;
        m_levelsSeeded = true;
    } else {
        m_noiseFloorDb += (noise - m_noiseFloorDb) * kNoiseSmoothing;
        const qreal peakRate = (peak > m_peakLevelDb) ? kPeakAttack : kPeakRelease;
        m_peakLevelDb += (peak - m_peakLevelDb) * peakRate;
    }

    if (m_publishPending)
        return;
    m_publishPending = true;
    // Siamo sul render thread: i segnali si emettono di là, non da qui.
    QMetaObject::invokeMethod(this, &PanadapterView::publishMeasuredLevels,
                              Qt::QueuedConnection);
}

void PanadapterView::publishMeasuredLevels()
{
    m_publishPending = false;
    emit measuredLevelsChanged();

    if (!m_autoRange)
        return;

    // Il fondo della scala sta *sopra* il fondo misurato: vedi
    // kFloorAboveNoiseDb nell'header per il perché — è la differenza fra un
    // waterfall in cui i segnali staccano e un muro di colore uniforme.
    const qreal floor = m_noiseFloorDb + kFloorAboveNoiseDb;
    const qreal ceiling = std::max(m_peakLevelDb + kCeilingAbovePeakDb, floor + kMinimumSpanDb);

    if (std::abs(floor - m_floorDb) > kPublishThresholdDb)
        setFloorDb(floor);
    if (std::abs(ceiling - m_ceilingDb) > kPublishThresholdDb)
        setCeilingDb(ceiling);
}

qreal PanadapterView::historySeconds() const
{
    if (m_rowsPerSecond <= 0.0 || m_historyRows <= 0)
        return 0.0;
    return m_historyRows / m_rowsPerSecond;
}

void PanadapterView::reportRowsConsumed(int rows, int historyRows)
{
    if (rows <= 0)
        return;

    m_historyRows = historyRows;
    m_rowsSinceTick += rows;

    if (!m_rowClock.isValid()) {
        m_rowClock.start();
        return;
    }

    // Una misura al secondo circa: più spesso si conterebbe soprattutto il
    // rumore del ritmo di rendering, e l'asse dei tempi ballerebbe sotto gli
    // occhi mentre lo si legge.
    const qint64 elapsed = m_rowClock.elapsed();
    if (elapsed < kRateWindowMs)
        return;

    const qreal rate = m_rowsSinceTick * 1000.0 / static_cast<qreal>(elapsed);
    m_rowsSinceTick = 0;
    m_rowClock.restart();

    if (m_rowsPerSecond <= 0.0)
        m_rowsPerSecond = rate;
    else
        m_rowsPerSecond += (rate - m_rowsPerSecond) * kRateSmoothing;

    if (m_ratePublishPending)
        return;
    m_ratePublishPending = true;
    // Siamo sul render thread: i segnali si emettono di là, non da qui.
    QMetaObject::invokeMethod(this, &PanadapterView::publishHistorySeconds,
                              Qt::QueuedConnection);
}

void PanadapterView::publishHistorySeconds()
{
    m_ratePublishPending = false;
    emit historySecondsChanged();
}

QStringList PanadapterView::paletteNames() const
{
    return waterfallPaletteNames();
}

void PanadapterView::setWaterfallMode(WaterfallMode mode)
{
    if (m_waterfallMode == mode)
        return;
    m_waterfallMode = mode;
    emit waterfallModeChanged();
    update();
}

void PanadapterView::setPaletteIndex(int index)
{
    index = qBound(0, index, paletteNames().size() - 1);
    if (m_paletteIndex == index)
        return;
    m_paletteIndex = index;
    emit paletteChanged();
    update();
}

void PanadapterView::setGamma(qreal value)
{
    // Sotto 0,2 l'immagine diventa una macchia bianca, sopra 3 sparisce tutto.
    value = qBound(0.2, value, 3.0);
    if (qFuzzyCompare(m_gamma, value))
        return;
    m_gamma = value;
    emit toneChanged();
    update();
}

void PanadapterView::setBlackThreshold(qreal value)
{
    // Oltre il 90% si taglierebbe anche il segnale, non solo il rumore.
    value = qBound(0.0, value, 0.9);
    if (qFuzzyCompare(m_blackThreshold, value))
        return;
    m_blackThreshold = value;
    emit toneChanged();
    update();
}

void PanadapterView::setTilt(qreal degrees)
{
    // A zero si guarderebbe la superficie di taglio, a novanta dall'alto:
    // fuori da questo intervallo la vista in rilievo non dice più nulla.
    degrees = qBound(15.0, degrees, 85.0);
    if (qFuzzyCompare(m_tilt, degrees))
        return;
    m_tilt = degrees;
    emit sceneChanged();
    update();
}

void PanadapterView::setRotation3d(qreal degrees)
{
    degrees = qBound(-45.0, degrees, 45.0);
    if (qFuzzyCompare(m_rotation3d, degrees))
        return;
    m_rotation3d = degrees;
    emit sceneChanged();
    update();
}

void PanadapterView::setReliefScale(qreal value)
{
    value = qBound(0.05, value, 1.5);
    if (qFuzzyCompare(m_reliefScale, value))
        return;
    m_reliefScale = value;
    emit sceneChanged();
    update();
}

void PanadapterView::setReliefGrid(qreal value)
{
    value = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(m_reliefGrid, value))
        return;
    m_reliefGrid = value;
    emit sceneChanged();
    update();
}

void PanadapterView::setFloorFlattening(qreal value)
{
    value = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(m_floorFlattening, value))
        return;
    m_floorFlattening = value;
    emit toneChanged();
    update();
}

void PanadapterView::setTimeSpan(qreal value)
{
    // Il minimo non è zero: sotto un decimo della memoria le righe diventano
    // fasce alte decine di pixel e l'immagine smette di essere un waterfall.
    value = qBound(0.1, value, 1.0);
    if (qFuzzyCompare(m_timeSpan, value))
        return;
    m_timeSpan = value;
    emit timeViewChanged();
    update();
}

void PanadapterView::setFrozen(bool frozen)
{
    if (m_frozen == frozen)
        return;
    m_frozen = frozen;
    emit timeViewChanged();
    update();
}

qreal PanadapterView::levelAt(qreal bandFraction) const
{
    if (m_lastRow.empty())
        return m_floorDb;

    const auto count = static_cast<qreal>(m_lastRow.size());
    const auto index = static_cast<std::size_t>(
        qBound<qreal>(0.0, bandFraction * (count - 1.0), count - 1.0));
    const float value = m_lastRow[index];
    return std::isfinite(value) ? static_cast<qreal>(value) : m_floorDb;
}

void PanadapterView::setTraceColor(const QColor &color)
{
    if (m_traceColor == color)
        return;
    m_traceColor = color;
    emit colorsChanged();
    update();
}

void PanadapterView::setFillColor(const QColor &color)
{
    if (m_fillColor == color)
        return;
    m_fillColor = color;
    emit colorsChanged();
    update();
}

void PanadapterView::setPeakColor(const QColor &color)
{
    if (m_peakColor == color)
        return;
    m_peakColor = color;
    emit colorsChanged();
    update();
}

void PanadapterView::setPeakHold(bool enabled)
{
    if (m_peakHold == enabled)
        return;
    m_peakHold = enabled;
    emit peakHoldChanged();
    update();
}

void PanadapterView::setPeakDecayDb(qreal dbPerSecond)
{
    // Sotto un decibel al secondo la riga non scende più: un picco casuale
    // resterebbe lì per minuti e la traccia diventerebbe l'inviluppo di tutto
    // ciò che è passato. Sopra i sessanta smette di essere una tenuta e torna
    // a essere la traccia istantanea, solo più lenta.
    dbPerSecond = qBound(1.0, dbPerSecond, 60.0);
    if (qFuzzyCompare(m_peakDecayDb, dbPerSecond))
        return;
    m_peakDecayDb = dbPerSecond;
    emit peakHoldChanged();
    update();
}

void PanadapterView::setBackgroundColor(const QColor &color)
{
    if (m_backgroundColor == color)
        return;
    m_backgroundColor = color;
    emit colorsChanged();
    update();
}

} // namespace dsdr::app
