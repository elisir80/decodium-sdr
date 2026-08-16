// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — renderer del panadattatore (thread di rendering).
//
// Regola di QQuickRhiItem: nulla è condiviso con l'item se non attraverso
// `synchronize()`, che gira sul render thread mentre il thread GUI è fermo.
#pragma once

#include "app/PanadapterView.h"

#include <QColor>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QQuickRhiItem>

#include <rhi/qrhi.h>

#include <memory>
#include <vector>

namespace dsdr::core {
class SpectrumFeed;
}

namespace dsdr::app {

class PanadapterRenderer : public QQuickRhiItemRenderer
{
public:
    PanadapterRenderer();
    ~PanadapterRenderer() override;

    void initialize(QRhiCommandBuffer *cb) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    bool createPipelines();
    bool ensureSpectrumResources(QRhiResourceUpdateBatch *batch);
    bool ensureReliefResources(QRhiResourceUpdateBatch *batch);
    void trackBinFloor();
    void uploadRows(QRhiResourceUpdateBatch *batch);
    void updateTraceGeometry(QRhiResourceUpdateBatch *batch);
    void updatePeakGeometry(QRhiResourceUpdateBatch *batch);
    void releaseSpectrumResources();
    void mirrorFetchedRows();

    QRhi *m_rhi = nullptr;

    // Risorse indipendenti dai dati.
    std::unique_ptr<QRhiBuffer> m_quadVbuf;
    std::unique_ptr<QRhiBuffer> m_waterfallUbuf;
    std::unique_ptr<QRhiBuffer> m_traceUbuf;
    std::unique_ptr<QRhiBuffer> m_fillUbuf;
    std::unique_ptr<QRhiBuffer> m_peakUbuf;
    std::unique_ptr<QRhiSampler> m_linearSampler;
    std::unique_ptr<QRhiTexture> m_colorMapTexture;
    std::unique_ptr<QRhiGraphicsPipeline> m_waterfallPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_tracePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_fillPipeline;
    std::unique_ptr<QRhiShaderResourceBindings> m_traceSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_fillSrb;
    // La riga dei massimi riusa la pipeline della traccia — stessa topologia,
    // stessi shader, stesso layout di vertici — e cambia solo il gruppo di
    // risorse, perché l'unica cosa che la distingue è il colore.
    std::unique_ptr<QRhiShaderResourceBindings> m_peakSrb;

    // Vista in rilievo: mesh e pipeline propri, ma la stessa texture ad anello
    // e la stessa colormap della vista piatta. Cambia come si guarda la
    // storia, non come la si raccoglie.
    std::unique_ptr<QRhiGraphicsPipeline> m_reliefPipeline;
    std::unique_ptr<QRhiShaderResourceBindings> m_reliefSrb;
    std::unique_ptr<QRhiBuffer> m_reliefUbuf;
    std::unique_ptr<QRhiBuffer> m_reliefVbuf;
    std::unique_ptr<QRhiBuffer> m_reliefIbuf;
    QShader m_reliefVertShader;
    QShader m_reliefFragShader;
    quint32 m_reliefIndexCount = 0;
    bool m_reliefUnavailable = false;

    // Risorse legate al numero di bin.
    std::unique_ptr<QRhiTexture> m_waterfallTexture;
    std::unique_ptr<QRhiShaderResourceBindings> m_waterfallSrb;
    std::unique_ptr<QRhiBuffer> m_traceVbuf;
    std::unique_ptr<QRhiBuffer> m_fillVbuf;
    std::unique_ptr<QRhiBuffer> m_peakVbuf;

    int m_binCount = 0;
    int m_writeRow = 0;
    int m_uploadedPalette = -1;
    bool m_quadUploaded = false;

    // Stato copiato dall'item in synchronize().
    core::SpectrumFeed *m_feed = nullptr;
    float m_floorDb = -130.0f;
    float m_ceilingDb = -20.0f;
    float m_spectrumRatio = 0.42f;
    float m_viewStart = 0.0f;
    float m_viewSpan = 1.0f;
    float m_autoRangeStart = 0.0f;
    float m_autoRangeSpan = 1.0f;
    bool m_mirrorSideband = false;
    bool m_mirrorLowerSideband = false;
    PanadapterView::WaterfallMode m_mode = PanadapterView::Flat;
    int m_paletteIndex = 0;
    float m_gamma = 0.85f;
    float m_blackThreshold = 0.06f;
    float m_tilt = 58.0f;
    float m_rotation = 0.0f;
    float m_reliefScale = 0.45f;
    float m_reliefGrid = 0.35f;
    float m_floorFlattening = 0.0f;
    float m_timeSpan = 1.0f;
    bool m_frozen = false;
    QColor m_traceColor;
    QColor m_fillColor;
    QColor m_peakColor;
    QColor m_backgroundColor;
    bool m_peakHold = true;
    float m_peakDecayDb = 12.0f;

    // Dati del frame corrente.
    std::vector<float> m_fetched;     ///< righe estratte dal feed
    std::vector<uchar> m_rowBytes;    ///< riga convertita per la texture
    std::vector<float> m_traceVertices;
    std::vector<float> m_fillVertices;
    std::vector<float> m_latestRow;
    /// Riga non trasformata per meter e autoscala. `m_fetched` può invece
    /// ricevere una copia speculare destinata esclusivamente al rendering.
    std::vector<float> m_measurementRow;
    int m_pendingRows = 0;
    bool m_traceDirty = false;

    // ── Tenuta dei picchi ────────────────────────────────────────────────
    //
    // I massimi si tengono in decibel, non in frazione di scala: fondo e vetta
    // si muovono da soli quando l'auto-range lavora, e un massimo memorizzato
    // come posizione sullo schermo scivolerebbe insieme a loro — la riga
    // sembrerebbe salire mentre la scala si stringe.
    /// Stima del fondo bin per bin, in decibel.
    ///
    /// È quello che rende possibile appiattire la pendenza del rumore lungo la
    /// banda senza toccare la scala: vedi `PanadapterView::floorFlattening`.
    std::vector<float> m_binFloorDb;

    std::vector<float> m_peakRow;
    std::vector<float> m_peakVertices;
    QElapsedTimer m_peakClock;
    bool m_peakSeeded = false;
};

} // namespace dsdr::app
