// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — renderer del panadattatore (thread di rendering).
//
// Regola di QQuickRhiItem: nulla è condiviso con l'item se non attraverso
// `synchronize()`, che gira sul render thread mentre il thread GUI è fermo.
#pragma once

#include "app/PanadapterView.h"

#include <QColor>
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
    void uploadRows(QRhiResourceUpdateBatch *batch);
    void updateTraceGeometry(QRhiResourceUpdateBatch *batch);
    void releaseSpectrumResources();

    QRhi *m_rhi = nullptr;

    // Risorse indipendenti dai dati.
    std::unique_ptr<QRhiBuffer> m_quadVbuf;
    std::unique_ptr<QRhiBuffer> m_waterfallUbuf;
    std::unique_ptr<QRhiBuffer> m_traceUbuf;
    std::unique_ptr<QRhiBuffer> m_fillUbuf;
    std::unique_ptr<QRhiSampler> m_linearSampler;
    std::unique_ptr<QRhiTexture> m_colorMapTexture;
    std::unique_ptr<QRhiGraphicsPipeline> m_waterfallPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_tracePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_fillPipeline;
    std::unique_ptr<QRhiShaderResourceBindings> m_traceSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_fillSrb;

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
    PanadapterView::WaterfallMode m_mode = PanadapterView::Flat;
    int m_paletteIndex = 0;
    float m_gamma = 0.85f;
    float m_blackThreshold = 0.06f;
    float m_tilt = 58.0f;
    float m_rotation = 0.0f;
    float m_reliefScale = 0.45f;
    QColor m_traceColor;
    QColor m_fillColor;
    QColor m_backgroundColor;

    // Dati del frame corrente.
    std::vector<float> m_fetched;     ///< righe estratte dal feed
    std::vector<uchar> m_rowBytes;    ///< riga convertita per la texture
    std::vector<float> m_traceVertices;
    std::vector<float> m_fillVertices;
    std::vector<float> m_latestRow;
    int m_pendingRows = 0;
    bool m_traceDirty = false;
};

} // namespace dsdr::app
